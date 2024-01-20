using System.Runtime.InteropServices;

namespace System.Reflection;

[StructLayout(LayoutKind.Sequential)]
internal sealed class RuntimeAssembly : Assembly
{
    private RuntimeAssembly[] _assemblyRefs;
    private RuntimeTypeInfo[] _typeRefs;
    
    private RuntimeTypeInfo[] _typeDefs;
    private MethodBase[] _methodDefs;
    private RuntimeFieldInfo[] _fields;

    private RuntimeMethodInfo _entryPoint;
}