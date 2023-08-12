#include "tinydotnet/types/type.h"
#include "tinydotnet/jit/jit.h"
#include "util/except.h"
#include "util/stb_ds.h"
#include "tinydotnet/disasm.h"
#include "util/string_builder.h"
#include "jit_internal.h"

// TODO: lock

#include <spidir/spidir.h>
#include <stdbool.h>

static spidir_module_handle_t m_spidir_module;

// buildin functions
static spidir_function_t m_builtin_memset;
static spidir_function_t m_builtin_memcpy;

tdn_err_t tdn_jit_init() {
    tdn_err_t err = TDN_NO_ERROR;

    m_spidir_module = spidir_module_create();

    //
    // Create built-in types
    //
    m_builtin_memset = spidir_module_create_extern_function(m_spidir_module,
                                         "memset",
                                         SPIDIR_TYPE_PTR,
                                         3, (spidir_value_type_t[]){
                                                SPIDIR_TYPE_PTR,
                                                SPIDIR_TYPE_I32,
                                                SPIDIR_TYPE_I64 });
    m_builtin_memcpy = spidir_module_create_extern_function(m_spidir_module,
                                                            "memcpy",
                                                            SPIDIR_TYPE_PTR,
                                                            3, (spidir_value_type_t[]){
                                                SPIDIR_TYPE_PTR,
                                                SPIDIR_TYPE_PTR,
                                                SPIDIR_TYPE_I64 });

cleanup:
    return err;
}

static spidir_dump_status_t stdout_dump_callback(const char* s, size_t size, void* ctx) {
    (void) ctx;
    tdn_host_printf("%.*s", size, s);
    return SPIDIR_DUMP_CONTINUE;
}

void tdn_jit_dump() {
    spidir_module_dump(m_spidir_module, stdout_dump_callback, NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPIDIR helpers
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static spidir_value_type_t get_spidir_argument_type(RuntimeTypeInfo type) {
    if (
        type == tSByte || type == tByte ||
        type == tInt16 || type == tUInt16 ||
        type == tInt32 || type == tUInt32 ||
        type == tBoolean
    ) {
        return SPIDIR_TYPE_I32;
    } else if (type->BaseType == tEnum) {
        return get_spidir_argument_type(type->EnumUnderlyingType);
    } else if (
        type == tInt64 || type == tUInt64 ||
        type == tIntPtr || type == tUIntPtr
    ) {
        return SPIDIR_TYPE_I64;
    } else if (type == tVoid) {
        return SPIDIR_TYPE_NONE;
    } else if (tdn_type_is_valuetype(type)) {
        // pass by-reference, copied by the caller
        return SPIDIR_TYPE_PTR;
    } else {
        // this is def a pointer
        return SPIDIR_TYPE_PTR;
    }
}


static spidir_value_type_t get_spidir_return_type(RuntimeTypeInfo type) {
    if (
        type == tSByte || type == tByte ||
        type == tInt16 || type == tUInt16 ||
        type == tInt32 || type == tUInt32 ||
        type == tBoolean
    ) {
        return SPIDIR_TYPE_I32;
    } else if (type->BaseType == tEnum) {
        return get_spidir_return_type(type->EnumUnderlyingType);
    } else if (
        type == tInt64 || type == tUInt64 ||
        type == tIntPtr || type == tUIntPtr
    ) {
        return SPIDIR_TYPE_I64;
    } else if (type == tVoid) {
        return SPIDIR_TYPE_NONE;
    } else if (tdn_type_is_valuetype(type)) {
        // argument is passed by reference
        return SPIDIR_TYPE_NONE;
    } else {
        // this is def a pointer
        return SPIDIR_TYPE_PTR;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The jitter itself
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SWAP(a, b) \
    do { \
        typeof(a) __temp = a; \
        a = b; \
        b = __temp; \
    } while (0)

/**
 * Helper to check if a type is a struct type and not any other type
 */
static bool jit_is_struct_type(RuntimeTypeInfo type) {
    type = tdn_get_intermediate_type(type);
    return
        tdn_type_is_valuetype(type) &&
        type != tInt32 &&
        type != tInt64 &&
        type != tIntPtr;
}

/**
 * Emit a memcpy with a known size
 */
static void jit_emit_memcpy(spidir_builder_handle_t builder, spidir_value_t ptr1, spidir_value_t ptr2, size_t size) {
    spidir_builder_build_call(builder, SPIDIR_TYPE_NONE, m_builtin_memcpy, 3,
                              (spidir_value_t[]){
                                        ptr1, ptr2,
                                        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, size)
                                    });
}

static tdn_err_t jit_resolve_parameter_type(RuntimeMethodBase method, int arg, RuntimeTypeInfo* type) {
    tdn_err_t err = TDN_NO_ERROR;

    // resolve the argument type
    RuntimeTypeInfo arg_type = NULL;
    if (arg == 0 && !method->Attributes.Static) {
        // non-static method's first arg is the this
        if (tdn_type_is_valuetype(method->DeclaringType)) {
            CHECK_AND_RETHROW(tdn_get_byref_type(method->DeclaringType, &arg_type));
        } else {
            arg_type = method->DeclaringType;
        }

    } else {
        // this is not included in Parameters list
        uint16_t param_arg = arg - (method->Attributes.Static ? 0 : 1);

        // get the correct argument
        CHECK(param_arg < method->Parameters->Length);
        arg_type = method->Parameters->Elements[param_arg]->ParameterType;
    }

    // return it
    *type = arg_type;

cleanup:
    return err;
}

static void jit_method_callback(spidir_builder_handle_t builder, void* _ctx) {
    jit_context_t* ctx = _ctx;
    tdn_err_t err = TDN_NO_ERROR;
    jit_label_t* labels = NULL;
    RuntimeMethodBase method = ctx->method;
    struct {
        spidir_value_t value;
        RuntimeTypeInfo type;
        bool spilled;
    }* args = NULL;
    eval_stack_t stack = {
        .max_depth = method->MethodBody->MaxStackSize
    };

    // take into account the first parameter might be an implicit
    // struct return pointer, we will just check if the stack size
    // is larger than 64bit, which can't be anything other than a
    // struct
    int args_offset = 0;
    if (method->ReturnParameter->ParameterType->StackSize > sizeof(uint64_t)) {
        args_offset = 1;
    }

    TRACE("%U::%U", ctx->method->DeclaringType->Name, ctx->method->Name);

    // get the this_type for future use
    RuntimeTypeInfo this_type = NULL;
    if (!method->Attributes.Static) {
        this_type = method->DeclaringType;
        if (tdn_type_is_valuetype(this_type)) {
            // this is a valuetype, the this is a reference
            CHECK_AND_RETHROW(tdn_get_byref_type(this_type, &this_type));
        }
    }

    // prepare table that will either save an argument reference or a spill,
    // fill with invalid entries
    int arg_count = (method->Parameters->Length + (this_type == NULL ? 0 : 1));
    args = tdn_host_mallocz(sizeof(*args) * arg_count);
    CHECK(args != NULL);
    for (int i = 0; i < arg_count; i++) {
        // resolve the parameter type
        RuntimeTypeInfo type;
        CHECK_AND_RETHROW(jit_resolve_parameter_type(method, i, &type));

        args[i].value = SPIDIR_VALUE_INVALID;
        args[i].type = type;
        args[i].spilled = false;
    }

    //
    // first pass, find all of the labels, this will
    // also create all the different basic blocks on
    // the way
    //

    // entry block
    jit_label_t* entry_label = jit_add_label(&labels, 0);
    CHECK(entry_label != NULL);
    entry_label->block = spidir_builder_create_block(builder);
    spidir_builder_set_entry_block(builder, entry_label->block);
    spidir_builder_set_block(builder, entry_label->block);

    // get the rest of the blocks by creating the labels
    bool spilled = false;
    uint32_t pc = 0;
    tdn_il_control_flow_t flow_control = TDN_IL_NEXT;
    while (pc != ctx->method->MethodBody->ILSize) {
        // decode instruction
        tdn_il_inst_t inst;
        CHECK_AND_RETHROW(tdn_disasm_inst(method, pc, &inst));

        // check if we need to spill an argument
        if (inst.opcode == CEE_LDARGA || inst.opcode == CEE_STARG) {
            int arg = inst.operand.variable;
            CHECK(arg < arg_count);
            RuntimeTypeInfo type = args[arg].type;

            // create a stackslot for the spill
            if (!args[arg].spilled) {
                args[arg].value = spidir_builder_build_stackslot(builder, type->StackSize, type->StackAlignment);
                args[arg].spilled = true;

                // store it, if its a value-type we need to copy it instead
                spidir_value_t param_ref = spidir_builder_build_param_ref(builder, args_offset + arg);
                if (jit_is_struct_type(type)) {
                    jit_emit_memcpy(builder,
                                    args[arg].value,
                                    param_ref,
                                    type->StackSize);
                } else {
                    spidir_builder_build_store(builder, param_ref, args[arg].value);
                }
            }

            // mark that we had a spill
            spilled = true;
        }

        // check the operand if it has any target, if it has we need to create all
        // the related labels
        // TODO: do switch
        if (inst.operand_type == TDN_IL_BRANCH_TARGET) {
            // add label for the destination
            jit_label_t* label = jit_add_label(&labels, inst.operand.branch_target);
            if (label != NULL) {
                label->block = spidir_builder_create_block(builder);
            }
        } else if (inst.operand_type == TDN_IL_SWITCH) {
            CHECK_FAIL();
        }

        // check how we got here, if we got here from one
        // of the following control flows we need to add
        // a label onto ourselves
        if (
            flow_control == TDN_IL_RETURN ||
            flow_control == TDN_IL_BRANCH ||
            flow_control == TDN_IL_COND_BRANCH ||
            flow_control == TDN_IL_THROW
        ) {
            jit_label_t* label = jit_add_label(&labels, pc);
            if (label != NULL) {
                label->block = spidir_builder_create_block(builder);
            }
        }

        pc += inst.length;
        flow_control = inst.control_flow;
    }

    // if we had a spill, the label at the start is no longer
    // actually at the start since it has spills, it should be
    // right after that
    if (spilled) {
        spidir_block_t entry = entry_label->block;
        entry_label->block = spidir_builder_create_block(builder);
        spidir_builder_build_branch(builder, entry_label->block);
        spidir_builder_set_block(builder, entry_label->block);
    }

    // TODO: support protected blocks properly

    //
    // the main jit function
    //

    pc = 0;
    flow_control = TDN_IL_NEXT;
    int label_idx = 1; // skip the entry label,
                       // since its a special case
    while (pc != ctx->method->MethodBody->ILSize) {
        tdn_il_inst_t inst;
        CHECK_AND_RETHROW(tdn_disasm_inst(method, pc, &inst));
        uint32_t next_pc = pc + inst.length;

        // For debug, print the instruction
        tdn_host_printf("[*] \t\t\tIL_%04x: %s", pc, tdn_get_opcode_name(inst.opcode));
        switch (inst.operand_type) {
            case TDN_IL_BRANCH_TARGET: tdn_host_printf(" IL_%04x", inst.operand.branch_target); break;
            case TDN_IL_NO_OPERAND: break;
            case TDN_IL_VARIABLE: tdn_host_printf(" %d", inst.operand.variable); break;
            case TDN_IL_INT8: tdn_host_printf(" %d", inst.operand.int8); break;
            case TDN_IL_INT32: tdn_host_printf(" %d", inst.operand.int32); break;
            case TDN_IL_INT64: tdn_host_printf(" %lld", (long long int)inst.operand.int64); break;
            case TDN_IL_FLOAT32: tdn_host_printf(" %f", inst.operand.float32); break;
            case TDN_IL_FLOAT64: tdn_host_printf(" %f", inst.operand.float64); break;
            case TDN_IL_METHOD: tdn_host_printf(" %U:%U", inst.operand.method->DeclaringType->Name, inst.operand.method->Name); break; // TODO: better name printing
            case TDN_IL_FIELD: tdn_host_printf(" %U:%U", inst.operand.field->DeclaringType->Name, inst.operand.field->Name); break; // TODO: better name printing
            case TDN_IL_TYPE: tdn_host_printf(" %U", inst.operand.type->Name); break; // TODO: better name printing
            case TDN_IL_STRING: tdn_host_printf(" %U", inst.operand.string); break;
            case TDN_IL_SWITCH: CHECK_FAIL();
        }
        tdn_host_printf("\n");

        // normalize the instruction for easier processing now that we printed it
        tdn_normalize_inst(&inst);

        // if we are coming from an instruction that can not jump to us
        // then we must first clear our stack
        // TODO: if we already got a jump to here then we need to take that stack
        if (
            flow_control == TDN_IL_RETURN ||
            flow_control == TDN_IL_BRANCH ||
            flow_control == TDN_IL_THROW
        ) {
            eval_stack_clear(&stack);
        }

        // check if there are more labels, if we are at a label we
        // need to properly switch to it
        bool has_label = false;
        if (label_idx < arrlen(labels)) {
            if (pc == labels[label_idx].address) {
                // found the current label
                jit_label_t* label = &labels[label_idx];
                spidir_block_t block = label->block;
                has_label = true;

                // can't have a label between a
                // prefix and instruction, it must jump
                // to the first prefix
                CHECK(flow_control != TDN_IL_META);

                // check the last opcode to see how we got to this
                // new label
                if (
                    flow_control == TDN_IL_NEXT ||
                    flow_control == TDN_IL_BREAK ||
                    flow_control == TDN_IL_CALL
                ) {
                    CHECK_AND_RETHROW(eval_stack_move_to_slots(&stack, builder));

                    // we got from a normal instruction, insert a jump
                    spidir_builder_build_branch(builder, block);
                }

                // we are now at this block
                spidir_builder_set_block(builder, block);

                // and increment so we will check
                // against the next label
                label_idx++;
            }

            // make sure the label is always after or is at
            // the next pc, if there is another label
            if (label_idx < arrlen(labels)) {
                CHECK(labels[label_idx].address >= pc);
            }
        }

        // TODO: control flow handling

        //
        // Update the context for the jitting function
        //
        ctx->next_pc = next_pc;
        ctx->pc = pc;
        ctx->inst = &inst;
        ctx->stack = &stack;

        //
        // resolve the target and next label as needed
        //
        jit_label_t* target_label = NULL;
        jit_label_t* next_label = NULL;

        // if we have a branch target make sure we have the target label
        if (inst.operand_type == TDN_IL_BRANCH_TARGET) {
            target_label = jit_get_label(labels, inst.operand.branch_target);
            CHECK(target_label != NULL);
            // TODO: verify the stack consistency
        }

        // if this is a cond branch make sure we have a next label
        if (inst.control_flow == TDN_IL_COND_BRANCH) {
            next_label = jit_get_label(labels, next_pc);
            CHECK(next_label != NULL);
            // TODO: verify the stack consistency
        }

        //
        // the main instruction jitting
        // TODO: split this to multiple functions in different places
        //
        switch (inst.opcode) {
            // load an argument
            case CEE_LDARG: {
                uint16_t arg = inst.operand.variable;
                CHECK(arg < arg_count);

                // get the argument we are loading
                RuntimeTypeInfo arg_type = tdn_get_intermediate_type(args[arg].type);

                if (args[arg].spilled) {
                    // was spilled, this is a stack slot
                    if (jit_is_struct_type(arg_type)) {
                        // use memcpy
                        spidir_value_t location;
                        CHECK_AND_RETHROW(eval_stack_alloc(&stack, builder, arg_type, &location));
                        jit_emit_memcpy(builder, location, args[arg].value, arg_type->StackSize);
                    } else if (arg_type == tInt32) {
                        // use a 32bit load
                        CHECK_AND_RETHROW(eval_stack_push(&stack, arg_type,
                                                          spidir_builder_build_load(builder, SPIDIR_TYPE_I32, args[arg].value)));
                    } else if (arg_type == tInt64 || arg_type == tIntPtr) {
                        // use a 64bit load
                        CHECK_AND_RETHROW(eval_stack_push(&stack, arg_type,
                                                          spidir_builder_build_load(builder, SPIDIR_TYPE_I64, args[arg].value)));
                    } else {
                        // use a pointer load
                        CHECK_AND_RETHROW(eval_stack_push(&stack, arg_type,
                                                          spidir_builder_build_load(builder, SPIDIR_TYPE_PTR, args[arg].value)));
                    }
                } else {
                    spidir_value_t param_ref = spidir_builder_build_param_ref(builder, args_offset + arg);

                    // was not spilled, this is a param-ref
                    if (jit_is_struct_type(arg_type)) {
                        // passed by pointer, memcpy to the stack
                        spidir_value_t location;
                        CHECK_AND_RETHROW(eval_stack_alloc(&stack, builder, arg_type, &location));
                        jit_emit_memcpy(builder, location, param_ref, arg_type->StackSize);
                    } else {
                        // just push it
                        CHECK_AND_RETHROW(eval_stack_push(&stack, arg_type, param_ref));
                    }
                }
            } break;

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Object related
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////

            case CEE_LDFLD: {
                RuntimeFieldInfo field = inst.operand.field;

                // pop the item
                spidir_value_t obj;
                RuntimeTypeInfo obj_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &obj_type, &obj));

                // check this is either an object or a managed pointer
                CHECK(
                    obj_type->IsByRef ||
                    tdn_type_is_referencetype(obj_type) ||
                    jit_is_struct_type(obj_type)
                );

                // TODO: verify the field is contained within the given object

                // get the stack type of the field
                RuntimeTypeInfo field_type = inst.operand.field->FieldType;
                RuntimeTypeInfo value_type = tdn_get_intermediate_type(field_type);

                // figure the pointer to the field itself
                spidir_value_t field_ptr;
                if (field->Attributes.Static) {
                    // static field
                    // TODO: emit a null-check?
                    CHECK_FAIL();
                } else {
                    // instance field
                    if (field->FieldOffset == 0) {
                        // field is at offset zero, just load it
                        field_ptr = obj;
                    } else {
                        // build an offset to the field
                        field_ptr = spidir_builder_build_ptroff(builder, obj,
                                                          spidir_builder_build_iconst(builder,
                                                                                      SPIDIR_TYPE_I32,
                                                                                      field->FieldOffset));
                    }
                }

                // perform the actual load
                if (jit_is_struct_type(field_type)) {
                    // we are copying a struct to the stack
                    spidir_value_t value;
                    CHECK_AND_RETHROW(eval_stack_alloc(&stack, builder, value_type, &value));
                    jit_emit_memcpy(builder, value, field_ptr, field_type->StackSize);
                } else {
                    // we are copying a simpler value
                    // TODO: once supported set the load size
                    spidir_value_type_t type;
                    if (value_type == tInt32) {
                        type = SPIDIR_TYPE_I32;
                    } else if (value_type == tInt64 || value_type == tIntPtr) {
                        type = SPIDIR_TYPE_I64;
                    } else {
                        type = SPIDIR_TYPE_PTR;
                    }
                    spidir_value_t value = spidir_builder_build_load(builder, type, field_ptr);
                    CHECK_AND_RETHROW(eval_stack_push(&stack, value_type, value));
                }
            } break;

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Control flow
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////

            // unconditional branch
            case CEE_BR: {
                CHECK_AND_RETHROW(eval_stack_move_to_slots(&stack, builder));

                // a branch, emit the branch
                spidir_builder_build_branch(builder, target_label->block);
            } break;

            // conditional branches
            case CEE_BRFALSE:
            case CEE_BRTRUE: {
                // pop the item
                spidir_value_t value;
                RuntimeTypeInfo value_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value_type, &value));

                // ECMA-335 doesn't say brtrue takes in anything but
                // O and native int, but I think its just an oversight
                CHECK(
                    tdn_type_is_referencetype(value_type) ||
                    value_type == tInt32 ||
                    value_type == tInt64 ||
                    value_type == tIntPtr);

                // create the comparison
                spidir_icmp_kind_t kind = inst.opcode == CEE_BRTRUE ? SPIDIR_ICMP_NE : SPIDIR_ICMP_EQ;
                spidir_value_t cmp = spidir_builder_build_icmp(builder,
                                            SPIDIR_ICMP_NE,
                                            SPIDIR_TYPE_I32,
                                            value, spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, 0));

                // check if one of the blocks needs to have the stack in stack slots
                CHECK_AND_RETHROW(eval_stack_move_to_slots(&stack, builder));

                // a branch, emit the branch
                spidir_builder_build_brcond(builder, cmp, target_label->block, next_label->block);
            } break;

            // all the different compare and compare-and-branches
            // that we have
            case CEE_BEQ:
            case CEE_BGE:
            case CEE_BGT:
            case CEE_BLE:
            case CEE_BLT:
            case CEE_BNE_UN:
            case CEE_BGE_UN:
            case CEE_BGT_UN:
            case CEE_BLE_UN:
            case CEE_BLT_UN:
            case CEE_CEQ:
            case CEE_CGT:
            case CEE_CGT_UN:
            case CEE_CLT:
            case CEE_CLT_UN: {
                // pop the items
                spidir_value_t value1, value2;
                RuntimeTypeInfo value1_type, value2_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value2_type, &value2));
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value1_type, &value1));

                //
                // perform the binary comparison and branch operations check,
                // anything else can not be tested
                //
                if (value1_type == tInt32) {
                    CHECK(value2_type == tInt32 || value2_type == tIntPtr);

                } else if (value1_type == tInt64) {
                    CHECK(value2_type == tInt64);

                } else if (value1_type == tIntPtr) {
                    CHECK(value2_type == tInt32 || value2_type == tIntPtr);

                } else if (value1_type->IsByRef) {
                    // TODO: does this only apply to types
                    //       of the same reference? I assume
                    //       it does but we might need to change this
                    CHECK(value2_type == value1_type);

                } else if (tdn_type_is_referencetype(value1_type) && tdn_type_is_referencetype(value2_type)) {
                    CHECK(
                        inst.opcode == CEE_BEQ ||
                        inst.opcode == CEE_BNE_UN ||
                        inst.opcode == CEE_CEQ ||
                        inst.opcode == CEE_CGT_UN
                    );

                } else {
                    CHECK_FAIL();
                }

                // spidir only has the one side, need to flip for the other side
                spidir_icmp_kind_t kind;
                bool compare = false;
                switch (inst.opcode) {
                    case CEE_CEQ: compare = true;
                    case CEE_BEQ: kind = SPIDIR_ICMP_EQ; break;
                    case CEE_BGE: kind = SPIDIR_ICMP_SLE; SWAP(value1, value2); break;
                    case CEE_CGT: compare = true;
                    case CEE_BGT: kind = SPIDIR_ICMP_SLT; SWAP(value1, value2); break;
                    case CEE_BLE: kind = SPIDIR_ICMP_SLE; break;
                    case CEE_CLT: compare = true;
                    case CEE_BLT: kind = SPIDIR_ICMP_SLT; break;
                    case CEE_BNE_UN: kind = SPIDIR_ICMP_NE; break;
                    case CEE_BGE_UN: kind = SPIDIR_ICMP_ULE; SWAP(value1, value2); break;
                    case CEE_CGT_UN: compare = true;
                    case CEE_BGT_UN: kind = SPIDIR_ICMP_ULT; SWAP(value1, value2); break;
                    case CEE_BLE_UN: kind = SPIDIR_ICMP_ULE; break;
                    case CEE_CLT_UN: compare = true;
                    case CEE_BLT_UN: kind = SPIDIR_ICMP_ULT; break;
                    default: CHECK_FAIL();
                }

                // create the comparison
                spidir_value_t cmp = spidir_builder_build_icmp(builder,
                                                               kind,
                                                               SPIDIR_TYPE_I32,
                                                               value1, value2);

                // check if its a compare or not
                if (compare) {
                    // a compare, just push the result as an int32
                    eval_stack_push(&stack, tInt32, cmp);
                } else {
                    CHECK_AND_RETHROW(eval_stack_move_to_slots(&stack, builder));

                    // a branch, emit the branch
                    spidir_builder_build_brcond(builder, cmp, target_label->block, next_label->block);
                }
            } break;

            // return value from the function
            case CEE_RET: {
                RuntimeTypeInfo wanted_ret_type = method->ReturnParameter->ParameterType;
                if (wanted_ret_type == tVoid) {
                    spidir_builder_build_return(builder, SPIDIR_VALUE_INVALID);
                } else {
                    RuntimeTypeInfo ret_type;
                    spidir_value_t ret_value;
                    eval_stack_pop(&stack, builder, &ret_type, &ret_value);

                    // TODO: handle structs properly

                    // make sure the type is a valid return target
                    CHECK(tdn_type_verified_assignable_to(ret_type, wanted_ret_type));

                    spidir_builder_build_return(builder, ret_value);
                }
            } break;

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Stack manipulation
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////

            // Push an int32 to the stack
            case CEE_LDC_I4: {
                // NOTE: we are treating the value as a uint32 so it will not sign extend it
                eval_stack_push(&stack, tInt32,
                                spidir_builder_build_iconst(builder,
                                                            SPIDIR_TYPE_I32, inst.operand.uint32));
            } break;

            // Push an int64
            case CEE_LDC_I8: {
                eval_stack_push(&stack, tInt64,
                                spidir_builder_build_iconst(builder,
                                                            SPIDIR_TYPE_I64, inst.operand.uint64));
            } break;

            // Pop a value and ignore it
            case CEE_POP: {
                // pop the value and ignore it
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, NULL, NULL));
            } break;

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Math related
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////

            // shifts
            case CEE_SHL:
            case CEE_SHR:
            case CEE_SHR_UN: {
                // pop the items
                spidir_value_t value, shift_amount;
                RuntimeTypeInfo value_type, shift_amount_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &shift_amount_type, &shift_amount));
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value_type, &value));

                // type check
                CHECK(value_type == tInt32 || value_type == tInt64 || value_type == tIntPtr);
                CHECK(shift_amount_type == tInt32 || shift_amount_type == tIntPtr);

                // perform the operation
                spidir_value_t result_value;
                switch (inst.opcode) {
                    case CEE_SHL: result_value = spidir_builder_build_shl(builder, value, shift_amount); break;
                    case CEE_SHR: result_value = spidir_builder_build_ashr(builder, value, shift_amount); break;
                    case CEE_SHR_UN: result_value = spidir_builder_build_lshr(builder, value, shift_amount); break;
                    default: CHECK_FAIL();
                }

                // push it to the stack
                CHECK_AND_RETHROW(eval_stack_push(&stack, value_type, result_value));
            } break;

            // binary operations on either integers or floats
            case CEE_ADD:
            case CEE_SUB:
            case CEE_AND:
            case CEE_OR:
            case CEE_XOR:
            case CEE_MUL:
            case CEE_DIV:
            case CEE_DIV_UN: {
                // pop the items
                spidir_value_t value1, value2;
                RuntimeTypeInfo value1_type, value2_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value2_type, &value2));
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value1_type, &value1));

                // figure the type we are going to use
                RuntimeTypeInfo result = NULL;
                if (value1_type == tInt32) {
                    if (value2_type == tInt32) {
                        result = tInt32;
                    } else if (value2_type == tIntPtr) {
                        result = tIntPtr;
                    } else {
                        CHECK_FAIL();
                    }
                } else if (value1_type == tInt64) {
                    CHECK(value2_type == tInt64);
                    result = tInt64;
                } else if (value1_type == tIntPtr) {
                    if (value2_type == tInt32 || value2_type == tIntPtr) {
                        result = tIntPtr;
                    } else {
                        CHECK_FAIL();
                    }
                } else {
                    CHECK_FAIL();
                }

                // TODO: for floats make sure it is an instruction that can take floats

                // create the operation
                spidir_value_t result_value;
                switch (inst.opcode) {
                    case CEE_ADD: result_value = spidir_builder_build_iadd(builder, value1, value2); break;
                    case CEE_SUB: result_value = spidir_builder_build_isub(builder, value1, value2); break;
                    case CEE_AND: result_value = spidir_builder_build_and(builder, value1, value2); break;
                    case CEE_OR: result_value = spidir_builder_build_or(builder, value1, value2); break;
                    case CEE_XOR: result_value = spidir_builder_build_xor(builder, value1, value2); break;
                    case CEE_MUL: result_value = spidir_builder_build_imul(builder, value1, value2); break;
                    case CEE_DIV: result_value = spidir_builder_build_sdiv(builder, value1, value2); break;
                    case CEE_DIV_UN: result_value = spidir_builder_build_udiv(builder, value1, value2); break;
                    default: CHECK_FAIL();
                }

                // push it to the stack
                CHECK_AND_RETHROW(eval_stack_push(&stack, result, result_value));
            } break;

            // bitwise not, emulate with value ^ ~0
            case CEE_NOT: {
                // pop the item
                spidir_value_t value;
                RuntimeTypeInfo value_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value_type, &value));

                // type check, also get the ones value for the width
                // for the xor operation
                spidir_value_t ones;
                if (value_type == tInt32) {
                    ones = spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, (uint32_t)~0u);
                } else if (value_type == tInt64 || value_type == tIntPtr) {
                    ones = spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, (uint64_t)~0ull);
                } else {
                    CHECK_FAIL();
                }

                // create the operation
                spidir_value_t result_value = spidir_builder_build_xor(builder, value, ones);

                // push it to the stack
                CHECK_AND_RETHROW(eval_stack_push(&stack, value_type, result_value));
            } break;

            // negation, emulate with 0 - value
            case CEE_NEG: {
                // pop the item
                spidir_value_t value;
                RuntimeTypeInfo value_type;
                CHECK_AND_RETHROW(eval_stack_pop(ctx->stack, builder, &value_type, &value));

                // type check, also get the ones value for the width
                // for the xor operation
                spidir_value_type_t type;
                if (value_type == tInt32) {
                    type = SPIDIR_TYPE_I32;
                } else if (value_type == tInt64 || value_type == tIntPtr) {
                    type = SPIDIR_TYPE_I64;
                } else {
                    CHECK_FAIL();
                }

                // TODO: floating point

                // create the operation
                spidir_value_t zero = spidir_builder_build_iconst(builder, type, 0);
                spidir_value_t result_value = spidir_builder_build_isub(builder, zero, value);

                // push it to the stack
                CHECK_AND_RETHROW(eval_stack_push(&stack, value_type, result_value));
            } break;

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Misc operations
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////

            case CEE_NOP: {
                // do nothing
            } break;

            default:
                CHECK_FAIL();
        }

        // move the pc forward
        pc = next_pc;
        flow_control = inst.control_flow;
    }

    // make sure we went over all of the labels
    CHECK(label_idx == arrlen(labels));

cleanup:
    arrfree(labels);
    eval_stack_free(&stack);
    tdn_host_free(args);
}

static tdn_err_t jit_method(jit_context_t* ctx) {
    tdn_err_t err = TDN_NO_ERROR;
    spidir_value_type_t* params = NULL;
    RuntimeMethodBase method = ctx->method;

    //
    // create the spidir function
    //

    // handle the return value, we have a special case of returning a struct, if it can't be returned
    // by value then it will be returned by passing an implicit pointer
    spidir_value_type_t ret_type = get_spidir_return_type(method->ReturnParameter->ParameterType);
    if (ret_type == SPIDIR_TYPE_NONE && method->ReturnParameter->ParameterType != tVoid) {
        arrpush(params, SPIDIR_TYPE_PTR);
    }

    // handle the `this` argument, its always a
    // pointer (byref for struct types)
    if (!method->Attributes.Static) {
        arrpush(params, SPIDIR_TYPE_PTR);
    }

    // handle the arguments
    for (size_t i = 0; i < method->Parameters->Length; i++) {
        arrpush(params, get_spidir_argument_type(method->Parameters->Elements[i]->ParameterType));
    }

    // generate the name
    // TODO: something proper
    char test[6] = {0};
    static int i = 0;
    test[0] = 't';
    test[1] = 'e';
    test[2] = 's';
    test[3] = 't';
    test[4] = '0' + i++;
    test[5] = '\0';

    // create the function itself
    spidir_function_t func = spidir_module_create_function(
        m_spidir_module,
        test, ret_type, arrlen(params), params
    );

    spidir_module_build_function(m_spidir_module, func, jit_method_callback, ctx);

//    tdn_jit_dump();

cleanup:
    arrfree(params);

    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// High-level apis
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

tdn_err_t tdn_jit_method(RuntimeMethodBase methodInfo) {
    tdn_err_t err = TDN_NO_ERROR;

    jit_context_t ctx = {
        .method = methodInfo
    };
    CHECK_AND_RETHROW(jit_method(&ctx));

cleanup:
    return err;
}

tdn_err_t tdn_jit_type(RuntimeTypeInfo type) {
    tdn_err_t err = TDN_NO_ERROR;

    // jit all the virtual methods, as those are the one that can be called
    // by other stuff unknowingly, the rest are going to be jitted lazyily
    for (int i = 0; i < type->DeclaredMethods->Length; i++) {
        RuntimeMethodBase method = (RuntimeMethodBase)type->DeclaredMethods->Elements[i];
        if (!method->Attributes.Virtual) continue;
        CHECK_AND_RETHROW(tdn_jit_method(method));
    }

cleanup:
    return err;
}