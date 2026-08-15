// =============================================================================
// TD Engine — TDScript JS Code Generator (Tier 4)
//
// Walks the AST and emits a JavaScript source string that runs in either:
//   - Browser (against TDEngine.net + TDEngine.ecs)
//   - Node.js (against td-server's net runtime)
//
// The emitted code depends on `tdscript_runtime.js` which provides:
//   - Log.info / Log.warn / Log.error
//   - Network.broadcastNotification / Network.sendToClient
//   - Physics.checkVoxelCollision
//   - Vector3
//   - Math (mirror of JS Math)
//   - __td_repl(field) — wraps a `replicated` field in a setter that
//     notifies the server's replication system on mutation.
//   - __td_rpc_register(name, mode, fn) — registers an RPC handler.
//   - __td_rpc_call(peerId, name, args, mode) — invokes a remote RPC.
//
// Network qualifier semantics:
//   `replicated Vector3 playerPosition;`
//     → Object.defineProperty(this, 'playerPosition', { get, set })
//       where set also calls __td_repl_write('playerPosition', val).
//       Server is the source of truth; clients receive push updates.
//
//   `@rpc(reliable) public void receiveClientInput(...)`
//     → registers the method as a reliable RPC handler on the server.
//       Client invokes via __td_rpc_call(peerId, 'receiveClientInput', args, 'reliable').
//
//   `@rpc(unreliable) public void ...`
//     → same but uses unreliable channel (may drop, may reorder).
//
// Emitted code style:
//   - ES6 classes (one per TDScript `class`)
//   - PascalCase class names, camelCase methods/fields
//   - Strict mode
//   - No `eval`, no `require`, no `process` access
// =============================================================================
#pragma once

#include "ast.h"
#include <string>
#include <vector>

namespace td::tdscript {

class CodeGenJs {
public:
    CodeGenJs();

    // Generate JS source from a module AST.
    // Returns the JS source string. Diagnostics (warnings) are in m_diags.
    std::string generate(const NodePtr& module);

    const std::vector<Diagnostic>& diagnostics() const { return m_diags; }

private:
    std::vector<Diagnostic> m_diags;
    std::string m_out;
    int m_indent = 0;

    void emit(const std::string& s);
    void emitLine(const std::string& s);
    void emitIndent();
    void newline();
    void pushIndent() { m_indent++; }
    void popIndent() { m_indent--; }

    void emitModule(const NodePtr& n);
    void emitImport(const NodePtr& n);
    void emitStruct(const NodePtr& n);
    void emitClass(const NodePtr& n);
    void emitField(const NodePtr& n, bool isClassMember);
    void emitMethod(const NodePtr& n);
    void emitBlock(const NodePtr& n);
    void emitStatement(const NodePtr& n);
    void emitVarDecl(const NodePtr& n);
    void emitIf(const NodePtr& n);
    void emitFor(const NodePtr& n);
    void emitWhile(const NodePtr& n);
    void emitReturn(const NodePtr& n);
    void emitExpression(const NodePtr& n);
    void emitBinary(const NodePtr& n);
    void emitUnary(const NodePtr& n);
    void emitCall(const NodePtr& n);
    void emitMemberAccess(const NodePtr& n);
    void emitIndexAccess(const NodePtr& n);
    void emitAssign(const NodePtr& n);
    void emitCompoundAssign(const NodePtr& n);
    void emitIdentifier(const NodePtr& n);
    void emitLiteral(const NodePtr& n);

    // Map TDScript type names → JS runtime types (for documentation/defaults)
    std::string jsTypeFor(const std::string& tdType);
    std::string defaultInitFor(const std::string& tdType);
};

} // namespace td::tdscript
