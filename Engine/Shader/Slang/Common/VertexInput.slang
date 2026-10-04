// Opt-in marker for slang_gen_header.py. A struct carrying [YaVertexInput]
// that is a vertex-stage entry parameter is emitted as a C++ record plus
// a VertexInputField table (SPIR-V location, byte offset, components, scalar).
// Unmarked vertex inputs are not emitted, so existing generated headers stay
// unchanged. The attribute type itself is not a shader parameter and is not
// emitted either.
[__AttributeUsage(_AttributeTargets.Struct)]
struct YaVertexInputAttribute
{
};
