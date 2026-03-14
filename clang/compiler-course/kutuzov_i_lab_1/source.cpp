#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Attrs.inc"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

namespace {
class KutuzovVirtualWarningVisitor final : public clang::RecursiveASTVisitor<KutuzovVirtualWarningVisitor> {
public:
  explicit KutuzovVirtualWarningVisitor(clang::ASTContext *context) : m_context(context) {}
  bool VisitFunctionDecl(clang::FunctionDecl *func) {
    // Checking if function is a method
    clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
    if (method) {
      // Checking if this method is an override of some other method
      // AND that it has no 'override' specifier
      if (method->size_overridden_methods() > 0 && !method->hasAttr<clang::OverrideAttr>()) {
        // Generating a warning message
        clang::DiagnosticsEngine &diagnostics = m_context->getDiagnostics();

        unsigned warning_type_id = diagnostics.getCustomDiagID(
          clang::DiagnosticsEngine::Warning, 
          "method '%0' overrides a virtual method but has no 'override' specifier!");

        diagnostics.Report(method->getLocation(), warning_type_id) << method->getNameAsString();
      }
    }

    return true;
  }

private:
  clang::ASTContext *m_context;
};

class KutuzovVirtualWarningConsumer final : public clang::ASTConsumer {
public:
  explicit KutuzovVirtualWarningConsumer(clang::ASTContext *context) : m_visitor(context) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    m_visitor.TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  KutuzovVirtualWarningVisitor m_visitor;
};

class KutuzovVirtualWarning final : public clang::PluginASTAction {
public:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef) override {
    return std::make_unique<KutuzovVirtualWarningConsumer>(&ci.getASTContext());
  }

  bool ParseArgs(const clang::CompilerInstance &ci,
                 const std::vector<std::string> &args) override {
    return true;
  }
};
} // namespace

static clang::FrontendPluginRegistry::Add<KutuzovVirtualWarning>
    X("no_override_warnings", "Warns the user of virtual methods that are overriden withoug an 'override' specifier.");
