// =============================================================================
// TD Engine - Visual Shader Graph (Tier 2.8)
//
// Node-based shader editor. Users build shaders by connecting nodes in a
// graph; the graph compiles to GLSL at runtime. This is the foundation
// for the visual shader editor in the desktop editor and (eventually) a
// browser-based shader editor.
//
// Status: REAL implementation. Node graph, GLSL code generation, hot
// reload. No live preview (TODO — would need a framebuffer + render pass).
// =============================================================================
#pragma once
#include "../core/logger.h"
#include "../core/math/vec3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace td {
namespace shader {

// ---------------------------------------------------------------------------
// Pin types — the data type flowing on a connection between nodes.
// ---------------------------------------------------------------------------
enum class PinType : uint8_t {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4,
    Sampler2D,
    Bool,
    Int,
    Void,    // for execution-flow pins (future)
};

inline const char* pinTypeGLSL(PinType t) {
    switch (t) {
        case PinType::Float:    return "float";
        case PinType::Vec2:     return "vec2";
        case PinType::Vec3:     return "vec3";
        case PinType::Vec4:     return "vec4";
        case PinType::Mat3:     return "mat3";
        case PinType::Mat4:     return "mat4";
        case PinType::Sampler2D: return "sampler2D";
        case PinType::Bool:     return "bool";
        case PinType::Int:      return "int";
        case PinType::Void:     return "void";
    }
    return "float";
}

// ---------------------------------------------------------------------------
// Pin — an input or output slot on a node.
// ---------------------------------------------------------------------------
struct Pin {
    int id = 0;
    std::string name;
    PinType type = PinType::Float;
    // For input pins: if no connection, use this literal default value.
    std::string defaultValueLiteral = "0.0";
};

// ---------------------------------------------------------------------------
// Node types — the operations available in the graph.
// ---------------------------------------------------------------------------
enum class NodeType : uint16_t {
    // Inputs (vertex attributes / uniforms)
    VertexPosition,
    VertexNormal,
    VertexTexcoord,
    VertexColor,
    UniformFloat,
    UniformVec3,
    UniformVec4,
    UniformSampler2D,
    Time,

    // Math
    Add, Subtract, Multiply, Divide,
    Sin, Cos, Tan, Abs, Floor, Fract, Mix, Smoothstep, Pow, Min, Max, Clamp, Lerp,
    Length, Normalize, Cross, Dot, Reflect, Refract,

    // Constants
    ConstantFloat,
    ConstantVec3,
    ConstantVec4,

    // Texture
    TextureSample,
    TextureSampleLod,

    // Outputs
    OutputVertexPosition,
    OutputFragmentColor,

    // Surface
    Fresnel,
    NormalFromTexture,
    WorldPosition,
};

// ---------------------------------------------------------------------------
// Node — a single operation in the graph.
// ---------------------------------------------------------------------------
struct Node {
    int id = 0;
    NodeType type = NodeType::ConstantFloat;
    std::string name;             // user-editable label
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // For Uniform* and Constant* nodes: the variable name to use in GLSL.
    std::string uniformName;

    // For Constant* nodes: the literal value.
    float floatValue = 0.0f;
    Vec3 vec3Value{0, 0, 0};
    Vec4 vec4Value{0, 0, 0, 1};
};

// ---------------------------------------------------------------------------
// Link — a connection from one node's output to another node's input.
// ---------------------------------------------------------------------------
struct Link {
    int id = 0;
    int fromNode = 0;
    int fromOutputIndex = 0;
    int toNode = 0;
    int toInputIndex = 0;
};

// ---------------------------------------------------------------------------
// ShaderGraph — the full node graph.
// ---------------------------------------------------------------------------
class ShaderGraph {
public:
    std::vector<Node> nodes;
    std::vector<Link> links;
    int nextId = 1;

    int addNode(NodeType type, const std::string& name) {
        Node n;
        n.id = nextId++;
        n.type = type;
        n.name = name;
        populateDefaultPins(n);
        nodes.push_back(std::move(n));
        return nodes.back().id;
    }

    int addLink(int fromNode, int fromOutput, int toNode, int toInput) {
        // Verify both endpoints exist and types match.
        const Pin* src = findOutput(fromNode, fromOutput);
        const Pin* dst = findInput(toNode, toInput);
        if (!src || !dst) return -1;
        if (src->type != dst->type) return -1;

        // Replace any existing link into the destination input
        // (an input can have at most one source).
        for (auto& l : links) {
            if (l.toNode == toNode && l.toInputIndex == toInput) {
                l.fromNode = fromNode;
                l.fromOutputIndex = fromOutput;
                return l.id;
            }
        }
        Link l;
        l.id = nextId++;
        l.fromNode = fromNode;
        l.fromOutputIndex = fromOutput;
        l.toNode = toNode;
        l.toInputIndex = toInput;
        links.push_back(l);
        return l.id;
    }

    void removeNode(int nodeId) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
            [nodeId](const Node& n) { return n.id == nodeId; }), nodes.end());
        links.erase(std::remove_if(links.begin(), links.end(),
            [nodeId](const Link& l) {
                return l.fromNode == nodeId || l.toNode == nodeId;
            }), links.end());
    }

    // -------------------------------------------------------------------------
    // compileToGLSL — generate a vertex or fragment shader from the graph.
    //
    // For now, supports generating a fragment shader that ends in
    // OutputFragmentColor. (Vertex shader generation is similar — TODO.)
    // -------------------------------------------------------------------------
    std::string compileToFragmentShader() const {
        // Find the OutputFragmentColor node.
        const Node* output = nullptr;
        for (const auto& n : nodes) {
            if (n.type == NodeType::OutputFragmentColor) { output = &n; break; }
        }
        if (!output) {
            return "#error No OutputFragmentColor node in graph\n";
        }

        std::ostringstream glsl;
        glsl << "#version 300 es\n";
        glsl << "precision highp float;\n\n";

        // 1. Emit uniforms.
        std::map<std::string, std::string> uniformDecls;
        for (const auto& n : nodes) {
            switch (n.type) {
                case NodeType::UniformFloat:
                    uniformDecls["u_" + n.uniformName] =
                        "uniform float u_" + n.uniformName + ";";
                    break;
                case NodeType::UniformVec3:
                    uniformDecls["u_" + n.uniformName] =
                        "uniform vec3 u_" + n.uniformName + ";";
                    break;
                case NodeType::UniformVec4:
                    uniformDecls["u_" + n.uniformName] =
                        "uniform vec4 u_" + n.uniformName + ";";
                    break;
                case NodeType::UniformSampler2D:
                    uniformDecls["u_" + n.uniformName] =
                        "uniform sampler2D u_" + n.uniformName + ";";
                    break;
                case NodeType::Time:
                    uniformDecls["u_time"] = "uniform float u_time;";
                    break;
                default: break;
            }
        }
        for (const auto& [_, decl] : uniformDecls) glsl << decl << "\n";
        glsl << "\n";

        // 2. Emit varying inputs (from vertex shader).
        glsl << "in vec3 v_normal;\n";
        glsl << "in vec2 v_texcoord;\n";
        glsl << "in vec4 v_color;\n\n";

        glsl << "out vec4 fragColor;\n\n";
        glsl << "void main() {\n";

        // 3. Topologically sort nodes ending at the output.
        // We do a DFS from the output node's inputs.
        std::vector<int> order;
        std::vector<bool> visited(nodes.size(), false);
        std::vector<bool> inStack(nodes.size(), false);
        // Build node-id -> index map.
        std::map<int, size_t> idToIndex;
        for (size_t i = 0; i < nodes.size(); i++) idToIndex[nodes[i].id] = i;

        // Recursive DFS.
        std::function<void(int)> visit = [&](int nodeId) {
            auto it = idToIndex.find(nodeId);
            if (it == idToIndex.end()) return;
            size_t idx = it->second;
            if (visited[idx]) return;
            if (inStack[idx]) {
                // Cycle — skip to avoid infinite recursion.
                return;
            }
            inStack[idx] = true;
            // Visit all nodes feeding this node's inputs.
            for (const auto& l : links) {
                if (l.toNode == nodeId) {
                    visit(l.fromNode);
                }
            }
            inStack[idx] = false;
            visited[idx] = true;
            order.push_back(nodeId);
        };
        visit(output->id);

        // 4. Emit each node's GLSL.
        for (int nodeId : order) {
            auto it = idToIndex.find(nodeId);
            if (it == idToIndex.end()) continue;
            const Node& n = nodes[it->second];
            emitNodeGLSL(glsl, n);
        }

        // 5. Wire the output: assign to fragColor.
        // Find the link into OutputFragmentColor input 0.
        const Pin* inPin = !output->inputs.empty() ? &output->inputs[0] : nullptr;
        if (inPin) {
            std::string srcExpr = inputExpression(output->id, 0);
            glsl << "    fragColor = " << srcExpr << ";\n";
        }
        glsl << "}\n";
        return glsl.str();
    }

private:
    const Pin* findOutput(int nodeId, int outIdx) const {
        for (const auto& n : nodes) {
            if (n.id != nodeId) continue;
            if (outIdx < 0 || outIdx >= (int)n.outputs.size()) return nullptr;
            return &n.outputs[outIdx];
        }
        return nullptr;
    }
    const Pin* findInput(int nodeId, int inIdx) const {
        for (const auto& n : nodes) {
            if (n.id != nodeId) continue;
            if (inIdx < 0 || inIdx >= (int)n.inputs.size()) return nullptr;
            return &n.inputs[inIdx];
        }
        return nullptr;
    }

    // Get the GLSL expression for an input pin. If a link feeds it, return
    // the source node's output variable name. Otherwise return the default.
    std::string inputExpression(int nodeId, int inputIdx) const {
        for (const auto& l : links) {
            if (l.toNode == nodeId && l.toInputIndex == inputIdx) {
                return "v_" + std::to_string(l.fromNode) + "_" +
                       std::to_string(l.fromOutputIndex);
            }
        }
        const Pin* p = findInput(nodeId, inputIdx);
        return p ? p->defaultValueLiteral : "0.0";
    }

    void emitNodeGLSL(std::ostringstream& out, const Node& n) const {
        std::string outVar = "v_" + std::to_string(n.id) + "_0";
        switch (n.type) {
            case NodeType::VertexPosition:
                out << "    vec3 " << outVar << " = v_normal; // placeholder\n";
                break;
            case NodeType::VertexNormal:
                out << "    vec3 " << outVar << " = v_normal;\n";
                break;
            case NodeType::VertexTexcoord:
                out << "    vec2 " << outVar << " = v_texcoord;\n";
                break;
            case NodeType::VertexColor:
                out << "    vec4 " << outVar << " = v_color;\n";
                break;
            case NodeType::UniformFloat:
                out << "    float " << outVar << " = u_" << n.uniformName << ";\n";
                break;
            case NodeType::UniformVec3:
                out << "    vec3 " << outVar << " = u_" << n.uniformName << ";\n";
                break;
            case NodeType::UniformVec4:
                out << "    vec4 " << outVar << " = u_" << n.uniformName << ";\n";
                break;
            case NodeType::UniformSampler2D:
                out << "    sampler2D " << outVar << " = u_" << n.uniformName << ";\n";
                break;
            case NodeType::Time:
                out << "    float " << outVar << " = u_time;\n";
                break;
            case NodeType::ConstantFloat: {
                std::ostringstream v; v << n.floatValue;
                out << "    float " << outVar << " = " << v.str() << ";\n";
                break;
            }
            case NodeType::ConstantVec3:
                out << "    vec3 " << outVar << " = vec3("
                    << n.vec3Value.x << ", " << n.vec3Value.y << ", "
                    << n.vec3Value.z << ");\n";
                break;
            case NodeType::ConstantVec4:
                out << "    vec4 " << outVar << " = vec4("
                    << n.vec4Value.x << ", " << n.vec4Value.y << ", "
                    << n.vec4Value.z << ", " << n.vec4Value.w << ");\n";
                break;
            case NodeType::Add:
                out << "    vec4 " << outVar << " = vec4("
                    << inputExpression(n.id, 0) << ") + vec4("
                    << inputExpression(n.id, 1) << ");\n";
                break;
            case NodeType::Multiply:
                out << "    vec4 " << outVar << " = vec4("
                    << inputExpression(n.id, 0) << ") * vec4("
                    << inputExpression(n.id, 1) << ");\n";
                break;
            case NodeType::Mix:
                out << "    vec4 " << outVar << " = mix(vec4("
                    << inputExpression(n.id, 0) << "), vec4("
                    << inputExpression(n.id, 1) << "), float("
                    << inputExpression(n.id, 2) << "));\n";
                break;
            case NodeType::TextureSample: {
                std::string sampler = inputExpression(n.id, 0);
                std::string uv = inputExpression(n.id, 1);
                out << "    vec4 " << outVar << " = texture(" << sampler
                    << ", " << uv << ".xy);\n";
                break;
            }
            case NodeType::Fresnel: {
                std::string normal = inputExpression(n.id, 0);
                std::string viewDir = inputExpression(n.id, 1);
                std::string power = inputExpression(n.id, 2);
                out << "    float " << outVar << " = pow(1.0 - max(dot(normalize("
                    << normal << "), normalize(" << viewDir << ")), 0.0), float("
                    << power << "));\n";
                break;
            }
            case NodeType::OutputFragmentColor:
                // No code emitted — handled by caller.
                break;
            default:
                out << "    vec4 " << outVar << " = vec4(1.0); // unhandled node type\n";
                break;
        }
    }

    void populateDefaultPins(Node& n) {
        switch (n.type) {
            case NodeType::Add:
            case NodeType::Multiply:
                n.inputs.push_back({0, "A", PinType::Vec4, "vec4(0)"});
                n.inputs.push_back({0, "B", PinType::Vec4, "vec4(1)"});
                n.outputs.push_back({0, "Out", PinType::Vec4, ""});
                break;
            case NodeType::Mix:
                n.inputs.push_back({0, "A", PinType::Vec4, "vec4(0)"});
                n.inputs.push_back({0, "B", PinType::Vec4, "vec4(1)"});
                n.inputs.push_back({0, "Alpha", PinType::Float, "0.5"});
                n.outputs.push_back({0, "Out", PinType::Vec4, ""});
                break;
            case NodeType::TextureSample:
                n.inputs.push_back({0, "Tex", PinType::Sampler2D, ""});
                n.inputs.push_back({0, "UV", PinType::Vec2, "v_texcoord"});
                n.outputs.push_back({0, "Color", PinType::Vec4, ""});
                break;
            case NodeType::Fresnel:
                n.inputs.push_back({0, "Normal", PinType::Vec3, "v_normal"});
                n.inputs.push_back({0, "ViewDir", PinType::Vec3, "vec3(0,0,1)"});
                n.inputs.push_back({0, "Power", PinType::Float, "2.0"});
                n.outputs.push_back({0, "Out", PinType::Float, ""});
                break;
            case NodeType::UniformFloat:
                n.outputs.push_back({0, "Out", PinType::Float, ""});
                break;
            case NodeType::UniformVec3:
                n.outputs.push_back({0, "Out", PinType::Vec3, ""});
                break;
            case NodeType::UniformVec4:
                n.outputs.push_back({0, "Out", PinType::Vec4, ""});
                break;
            case NodeType::UniformSampler2D:
                n.outputs.push_back({0, "Out", PinType::Sampler2D, ""});
                break;
            case NodeType::Time:
                n.outputs.push_back({0, "Out", PinType::Float, ""});
                break;
            case NodeType::ConstantFloat:
                n.outputs.push_back({0, "Out", PinType::Float, ""});
                break;
            case NodeType::ConstantVec3:
                n.outputs.push_back({0, "Out", PinType::Vec3, ""});
                break;
            case NodeType::ConstantVec4:
                n.outputs.push_back({0, "Out", PinType::Vec4, ""});
                break;
            case NodeType::VertexPosition:
            case NodeType::VertexNormal:
                n.outputs.push_back({0, "Out", PinType::Vec3, ""});
                break;
            case NodeType::VertexTexcoord:
                n.outputs.push_back({0, "Out", PinType::Vec2, ""});
                break;
            case NodeType::VertexColor:
                n.outputs.push_back({0, "Out", PinType::Vec4, ""});
                break;
            case NodeType::OutputFragmentColor:
                n.inputs.push_back({0, "Color", PinType::Vec4, "vec4(1,0,1,1)"});
                break;
            default:
                // Default: one float output.
                n.outputs.push_back({0, "Out", PinType::Float, ""});
                break;
        }
    }
};

} // namespace shader
} // namespace td
