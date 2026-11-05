#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "codegen.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

const std::string VERSION = "1.0";

void printHelp(const std::string& prog) {
    std::cout << "Usage: " << prog << " [options] <input.zz>\n\n"
              << "Options:\n"
              << "  -h, --help       Show this help\n"
              << "  -v, --version    Show version\n"
              << "  -o <file>        Output binary path (default: a.out)\n";
}

bool emitObject(llvm::Module* mod, const std::string& objPath,
                const std::string& tripleStr) {
    std::string err;
    llvm::Triple triple(tripleStr);
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) { std::cerr << "target error: " << err << "\n"; return false; }

    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);

    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(triple, "generic", "", opt, rm));
    if (!tm) { std::cerr << "cannot create TargetMachine\n"; return false; }

    mod->setDataLayout(tm->createDataLayout());
    mod->setTargetTriple(triple);

    std::error_code ec;
    llvm::raw_fd_ostream dest(objPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "cannot open " << objPath << ": " << ec.message() << "\n";
        return false;
    }

    {
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, dest, nullptr,
                                     llvm::CodeGenFileType::ObjectFile)) {
            std::cerr << "target cannot emit object file\n";
            return false;
        }
        pm.run(*mod);
    }
    dest.flush();
    return true;
}

int main(int argc, char* argv[]) {
    std::string inputPath, outputPath;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")    { printHelp(argv[0]); return 0; }
        if (a == "-v" || a == "--version") {
            std::cout << "Zeuz v" << VERSION << "\n"
              << "Copyright (c) 2026 Zeuz Language\n"
              << "All rights reserved.\n";
          return 0;
        }
        if (a == "-o") {
            if (++i >= argc) { std::cerr << "-o requires an argument\n"; return 1; }
            outputPath = argv[i];
            continue;
        }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown option: " << a << "\n";
            return 1;
        }
        inputPath = a;
    }

    if (inputPath.empty()) {
        std::cerr << "no input file\n";
        printHelp(argv[0]);
        return 1;
    }

    std::ifstream file(inputPath);
    if (!file) { std::cerr << "cannot open " << inputPath << "\n"; return 1; }
    std::stringstream buf; buf << file.rdbuf();

    try {
        Lexer lexer(buf.str());
        Parser parser(lexer.tokenize());
        Program program = parser.parseProgram();

        SemanticAnalyzer sema;
        bool ok = sema.analyze(program);
        for (auto& e : sema.errors())   std::cerr << "error: "   << e << "\n";
        for (auto& w : sema.warnings()) std::cerr << "warning: " << w << "\n";
        if (!ok) return 1;

        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();

        std::string triple  = llvm::sys::getDefaultTargetTriple();
        std::string binPath = outputPath.empty() ? "a.out" : outputPath;
        std::string objPath = binPath + ".o";

        {
            llvm::LLVMContext ctx;
            Codegen cg;
            auto mod = cg.generate(program, sema, ctx);
            if (!mod) { std::cerr << "codegen error: " << cg.error() << "\n"; return 1; }

            if (!emitObject(mod.get(), objPath, triple)) return 1;
        } 

        std::string cc  = std::getenv("CC") ? std::getenv("CC") : "clang";
        std::string cmd = cc + " -o \"" + binPath + "\" \"" + objPath + "\" -lm";
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            std::cerr << "link failed; object kept at " << objPath << "\n";
            return 1;
        }
        std::remove(objPath.c_str());
        std::cout << "compiled to " << binPath << "\n";
        return 0;

    } catch (const Parser::ParseError& e) {
        std::cerr << inputPath << ":" << e.line << ":" << e.col
                  << ": parse error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
