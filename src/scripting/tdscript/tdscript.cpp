// =============================================================================
// TD Engine — TDScript Top-Level Compiler Entry (Tier 4)
// =============================================================================
#include "tdscript.h"
#include "lexer.h"
#include "parser.h"
#include "codegen_js.h"
#include <sstream>

namespace td::tdscript {

std::string compileSource(const std::string& source,
                          const std::string& target,
                          std::string* errorMsg) {
    // Lex
    Lexer lex(source);
    auto tokens = lex.tokenize();
    if (!lex.diagnostics().empty()) {
        if (errorMsg) {
            std::ostringstream oss;
            for (const auto& d : lex.diagnostics()) {
                oss << "[line " << d.line << ":" << d.col << "] " << d.message << "\n";
            }
            *errorMsg = oss.str();
        }
        // continue parsing best-effort
    }

    // Parse
    Parser parser(std::move(tokens));
    NodePtr module = parser.parseModule();
    if (!parser.diagnostics().empty()) {
        if (errorMsg) {
            std::ostringstream oss;
            for (const auto& d : parser.diagnostics()) {
                oss << "[line " << d.line << ":" << d.col << "] " << d.message << "\n";
            }
            if (!errorMsg->empty()) *errorMsg += "\n";
            *errorMsg += oss.str();
        }
    }

    if (!module) {
        if (errorMsg && errorMsg->empty()) *errorMsg = "parse failed (no module produced)";
        return "";
    }

    // Codegen
    if (target == "js" || target.empty()) {
        CodeGenJs cg;
        std::string out = cg.generate(module);
        return out;
    } else if (target == "cpp") {
        // C++ codegen is a stub for now. Emit a comment so callers know.
        if (errorMsg) *errorMsg = "C++ codegen target is not yet implemented (use 'js')";
        return "// TDScript C++ codegen not yet implemented. Use target=js.\n";
    }

    if (errorMsg) *errorMsg = "unknown target: " + target;
    return "";
}

} // namespace td::tdscript
