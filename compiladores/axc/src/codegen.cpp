/*
 * AxolynOS - Generador de codigo de Axolyn (bytecode AXB).
 */
#include "axc/codegen.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "axbytecode.h"
#include "axc/sema.h"

namespace axc {

namespace {

/* Tamano del operando de cada opcode (-1 si el opcode no existe). */
int opcodeOperandSize(uint8_t op) {
    switch (op) {
        case AX_OP_NOP:
        case AX_OP_POP:
        case AX_OP_DUP:
        case AX_OP_DUP2:
        case AX_OP_SWAP:
        case AX_OP_RET:
        case AX_OP_ADD: case AX_OP_SUB: case AX_OP_MUL: case AX_OP_DIV:
        case AX_OP_MOD: case AX_OP_NEG: case AX_OP_NOT: case AX_OP_BITNOT:
        case AX_OP_AND: case AX_OP_OR: case AX_OP_XOR:
        case AX_OP_SHL: case AX_OP_SHR: case AX_OP_USHR:
        case AX_OP_EQ: case AX_OP_NE: case AX_OP_LT: case AX_OP_LE:
        case AX_OP_GT: case AX_OP_GE:
        case AX_OP_DEREF: case AX_OP_STORE: case AX_OP_FREE:
        case AX_OP_ARRAY_GET: case AX_OP_ARRAY_SET: case AX_OP_ARRAY_LEN:
        case AX_OP_STR_LEN: case AX_OP_STR_CONCAT: case AX_OP_STR_EQ:
        case AX_OP_STR_CMP: case AX_OP_STR_CHAR_AT:
        case AX_OP_PRINT: case AX_OP_PRINTLN: case AX_OP_PRINT_I64:
        case AX_OP_YIELD: case AX_OP_HALT:
            return 0;
        case AX_OP_PUSH_I64:
        case AX_OP_NEW:
        case AX_OP_STRUCT_NEW:
            return 8;
        case AX_OP_CALL:
        case AX_OP_CALL_NATIVE:
            return 8;   /* dos u32 */
        case AX_OP_PUSH_STR:
        case AX_OP_LOAD_LOCAL: case AX_OP_STORE_LOCAL:
        case AX_OP_LOAD_GLOBAL: case AX_OP_STORE_GLOBAL:
        case AX_OP_LOCAL_ADDR: case AX_OP_GLOBAL_ADDR:
        case AX_OP_JMP: case AX_OP_JZ: case AX_OP_JNZ: case AX_OP_JLT: case AX_OP_JGE:
        case AX_OP_ARRAY_NEW:
        case AX_OP_STRUCT_GET: case AX_OP_STRUCT_SET:
        case AX_OP_SLEEP: case AX_OP_ASSERT:
            return 4;
        default:
            return -1;
    }
}

const char* opcodeName(uint8_t op) {
    switch (op) {
        case AX_OP_NOP: return "NOP";
        case AX_OP_PUSH_I64: return "PUSH_I64";
        case AX_OP_PUSH_STR: return "PUSH_STR";
        case AX_OP_POP: return "POP";
        case AX_OP_DUP: return "DUP";
        case AX_OP_DUP2: return "DUP2";
        case AX_OP_SWAP: return "SWAP";
        case AX_OP_LOAD_LOCAL: return "LOAD_LOCAL";
        case AX_OP_STORE_LOCAL: return "STORE_LOCAL";
        case AX_OP_LOAD_GLOBAL: return "LOAD_GLOBAL";
        case AX_OP_STORE_GLOBAL: return "STORE_GLOBAL";
        case AX_OP_LOCAL_ADDR: return "LOCAL_ADDR";
        case AX_OP_GLOBAL_ADDR: return "GLOBAL_ADDR";
        case AX_OP_JMP: return "JMP";
        case AX_OP_JZ: return "JZ";
        case AX_OP_JNZ: return "JNZ";
        case AX_OP_JLT: return "JLT";
        case AX_OP_JGE: return "JGE";
        case AX_OP_CALL: return "CALL";
        case AX_OP_CALL_NATIVE: return "CALL_NATIVE";
        case AX_OP_RET: return "RET";
        case AX_OP_ADD: return "ADD";
        case AX_OP_SUB: return "SUB";
        case AX_OP_MUL: return "MUL";
        case AX_OP_DIV: return "DIV";
        case AX_OP_MOD: return "MOD";
        case AX_OP_NEG: return "NEG";
        case AX_OP_NOT: return "NOT";
        case AX_OP_BITNOT: return "BITNOT";
        case AX_OP_AND: return "AND";
        case AX_OP_OR: return "OR";
        case AX_OP_XOR: return "XOR";
        case AX_OP_SHL: return "SHL";
        case AX_OP_SHR: return "SHR";
        case AX_OP_USHR: return "USHR";
        case AX_OP_EQ: return "EQ";
        case AX_OP_NE: return "NE";
        case AX_OP_LT: return "LT";
        case AX_OP_LE: return "LE";
        case AX_OP_GT: return "GT";
        case AX_OP_GE: return "GE";
        case AX_OP_DEREF: return "DEREF";
        case AX_OP_STORE: return "STORE";
        case AX_OP_NEW: return "NEW";
        case AX_OP_FREE: return "FREE";
        case AX_OP_ARRAY_NEW: return "ARRAY_NEW";
        case AX_OP_ARRAY_GET: return "ARRAY_GET";
        case AX_OP_ARRAY_SET: return "ARRAY_SET";
        case AX_OP_ARRAY_LEN: return "ARRAY_LEN";
        case AX_OP_STRUCT_NEW: return "STRUCT_NEW";
        case AX_OP_STRUCT_GET: return "STRUCT_GET";
        case AX_OP_STRUCT_SET: return "STRUCT_SET";
        case AX_OP_STR_LEN: return "STR_LEN";
        case AX_OP_STR_CONCAT: return "STR_CONCAT";
        case AX_OP_STR_EQ: return "STR_EQ";
        case AX_OP_STR_CMP: return "STR_CMP";
        case AX_OP_STR_CHAR_AT: return "STR_CHAR_AT";
        case AX_OP_PRINT: return "PRINT";
        case AX_OP_PRINTLN: return "PRINTLN";
        case AX_OP_PRINT_I64: return "PRINT_I64";
        case AX_OP_YIELD: return "YIELD";
        case AX_OP_SLEEP: return "SLEEP";
        case AX_OP_HALT: return "HALT";
        case AX_OP_ASSERT: return "ASSERT";
        default: return "???";
    }
}

uint32_t readU32(const std::vector<uint8_t>& code, size_t pos) {
    if (pos + 4 > code.size()) return 0;
    return (uint32_t)code[pos] | ((uint32_t)code[pos + 1] << 8) |
           ((uint32_t)code[pos + 2] << 16) | ((uint32_t)code[pos + 3] << 24);
}

uint64_t readU64(const std::vector<uint8_t>& code, size_t pos) {
    if (pos + 8 > code.size()) return 0;
    return (uint64_t)readU32(code, pos) | ((uint64_t)readU32(code, pos + 4) << 32);
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Desensamblado                                                       */
/* ------------------------------------------------------------------ */
std::string BytecodeImage::disassembly() const {
    std::ostringstream os;
    char buf[64];

    os << "; ----------------------------------------------------------\n";
    os << "; Imagen AXB v" << AXB_VERSION << " generada por axc\n";
    os << "; ----------------------------------------------------------\n";
    os << "; funciones: " << funcs.size() << "\n";
    for (const FuncInfo& f : funcs) {
        std::snprintf(buf, sizeof(buf), "0x%04x", (unsigned)f.addr);
        os << ";   " << buf << "  " << f.name << "  argc=" << f.argc
           << " locals=" << f.nlocals << " frame=" << f.stackSize << "\n";
    }
    os << "; globales: " << globals.size() << " (celdas: " << globalSlots << ")\n";
    for (const GlobalInfo& g : globals) {
        os << ";   slot " << g.slot << "  " << g.name << "\n";
    }
    os << "; natives: " << natives.size() << "\n";
    for (const NativeInfo& n : natives) {
        os << ";   id " << n.id << "  " << n.name << "\n";
    }
    os << "; imports: " << imports.size() << "\n";
    for (const ImportInfo& i : imports) {
        os << ";   " << i.ns << "  ids " << i.nativeBase << ".." << (i.nativeBase + i.count - 1)
           << "\n";
    }
    os << "; entry=" << entryFunc << " main=" << mainFunc << "\n";
    os << "; codigo: " << code.size() << " bytes\n";
    os << "; ----------------------------------------------------------\n";

    size_t pc = 0;
    while (pc < code.size()) {
        /* Marca el inicio de una funcion. */
        for (const FuncInfo& f : funcs) {
            if (f.addr == pc) {
                os << "\n; --- " << f.name << " ---\n";
            }
        }
        uint8_t op = code[pc];
        int operand = opcodeOperandSize(op);
        std::snprintf(buf, sizeof(buf), "%04x  ", (unsigned)pc);
        os << buf;
        os << std::left << std::setw(14) << opcodeName(op) << std::right;
        size_t next = pc + 1;
        if (operand == 4) {
            uint32_t v = readU32(code, pc + 1);
            std::snprintf(buf, sizeof(buf), "0x%08x  (%u)", (unsigned)v, (unsigned)v);
            os << buf;
            next = pc + 5;
        } else if (operand == 8) {
            uint64_t v = readU64(code, pc + 1);
            if (op == AX_OP_CALL || op == AX_OP_CALL_NATIVE) {
                uint32_t a = readU32(code, pc + 1);
                uint32_t b = readU32(code, pc + 5);
                std::snprintf(buf, sizeof(buf), "id=%u argc=%u", (unsigned)a, (unsigned)b);
                os << buf;
            } else {
                std::snprintf(buf, sizeof(buf), "0x%016llx  (%lld)", (unsigned long long)v,
                              (long long)v);
                os << buf;
            }
            next = pc + 9;
        } else {
            buf[0] = '\0';
            os << buf;
        }
        os << "\n";
        pc = next;
    }
    return os.str();
}

/* ------------------------------------------------------------------ */
/* Escritura del fichero .axb                                         */
/* ------------------------------------------------------------------ */
namespace {

void pushU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFFu));
    out.push_back((uint8_t)((v >> 8) & 0xFFu));
    out.push_back((uint8_t)((v >> 16) & 0xFFu));
    out.push_back((uint8_t)((v >> 24) & 0xFFu));
}

void pushU64(std::vector<uint8_t>& out, uint64_t v) {
    pushU32(out, (uint32_t)(v & 0xFFFFFFFFu));
    pushU32(out, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

} /* namespace */

bool writeAxBFile(const std::string& path, const BytecodeImage& image, std::string& error) {
    /* Tabla de cadenas: los literales primero (los indices los usa PUSH_STR)
     * y despues los nombres de funciones, globales, natives y namespaces.
     * Todos los "off" del formato son desplazamientos en BYTES dentro del
     * blob de cadenas (ver Kernel/vm/axbytecode.h). */
    std::vector<std::string> all = image.strings;
    auto addName = [&all](const std::string& s) -> uint32_t {
        for (size_t i = 0; i < all.size(); i++) {
            if (all[i] == s) return (uint32_t)i;
        }
        all.push_back(s);
        return (uint32_t)(all.size() - 1);
    };

    /* Primero se registran los nombres (pueden anadir entradas nuevas). */
    std::vector<uint32_t> funcNameIdx, globalNameIdx, nativeNameIdx, importNsIdx;
    for (const BytecodeImage::FuncInfo& f : image.funcs) funcNameIdx.push_back(addName(f.name));
    for (const BytecodeImage::GlobalInfo& g : image.globals) globalNameIdx.push_back(addName(g.name));
    for (const BytecodeImage::NativeInfo& n : image.natives) nativeNameIdx.push_back(addName(n.name));
    for (const BytecodeImage::ImportInfo& i : image.imports) importNsIdx.push_back(addName(i.ns));

    /* Blob de cadenas y tabla de desplazamientos. */
    std::vector<uint8_t> blob;
    std::vector<uint32_t> offsets;
    offsets.reserve(all.size());
    for (const std::string& s : all) {
        offsets.push_back((uint32_t)blob.size());
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back(0);
    }
    auto offOf = [&offsets](uint32_t idx) -> uint32_t {
        return idx < offsets.size() ? offsets[idx] : 0u;
    };

    AxbHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, AXB_MAGIC, AXB_MAGIC_LEN);
    header.version = AXB_VERSION;
    header.flags = 0;
    header.code_size = (uint32_t)image.code.size();
    header.const_count = (uint32_t)image.consts.size();
    header.str_count = (uint32_t)all.size();
    header.str_data_size = (uint32_t)blob.size();
    header.func_count = (uint32_t)image.funcs.size();
    header.global_count = (uint32_t)image.globals.size();
    header.native_count = (uint32_t)image.natives.size();
    header.import_count = (uint32_t)image.imports.size();
    header.entry_func = image.entryFunc;
    header.main_func = image.mainFunc;
    header.global_slots = image.globalSlots;
    header.reserved = 0;

    std::vector<uint8_t> out;
    out.reserve(4096);
    const uint8_t* h = (const uint8_t*)(const void*)&header;
    out.insert(out.end(), h, h + sizeof(header));

    out.insert(out.end(), image.code.begin(), image.code.end());
    for (int64_t c : image.consts) pushU64(out, (uint64_t)c);
    for (uint32_t o : offsets) pushU32(out, o);
    out.insert(out.end(), blob.begin(), blob.end());

    for (size_t i = 0; i < image.funcs.size(); i++) {
        pushU32(out, offOf(funcNameIdx[i]));
        pushU32(out, image.funcs[i].addr);
        pushU32(out, image.funcs[i].argc);
        pushU32(out, image.funcs[i].nlocals);
        pushU32(out, image.funcs[i].stackSize);
    }
    for (size_t i = 0; i < image.globals.size(); i++) {
        pushU32(out, offOf(globalNameIdx[i]));
        pushU32(out, image.globals[i].slot);
        pushU32(out, image.globals[i].size);
        pushU32(out, image.globals[i].flags);
    }
    for (size_t i = 0; i < image.natives.size(); i++) {
        pushU32(out, offOf(nativeNameIdx[i]));
        pushU32(out, image.natives[i].id);
    }
    for (size_t i = 0; i < image.imports.size(); i++) {
        pushU32(out, offOf(importNsIdx[i]));
        pushU32(out, image.imports[i].nativeBase);
        pushU32(out, image.imports[i].count);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "no se puede abrir '" + path + "' para escritura";
        return false;
    }
    file.write((const char*)out.data(), (std::streamsize)out.size());
    if (!file) {
        error = "error al escribir '" + path + "'";
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* CodeGen                                                            */
/* ------------------------------------------------------------------ */
void CodeGen::error(const SourcePos& pos, const std::string& msg) {
    if (diags_) diags_->emplace_back(pos.line, pos.column, msg);
    failed_ = true;
}

void CodeGen::emit(uint8_t op) {
    code_.push_back(op);
}

void CodeGen::emitU32(uint32_t v) {
    code_.push_back((uint8_t)(v & 0xFFu));
    code_.push_back((uint8_t)((v >> 8) & 0xFFu));
    code_.push_back((uint8_t)((v >> 16) & 0xFFu));
    code_.push_back((uint8_t)((v >> 24) & 0xFFu));
}

void CodeGen::emitU64(uint64_t v) {
    emitU32((uint32_t)(v & 0xFFFFFFFFu));
    emitU32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

void CodeGen::emitPushI64(int64_t v) {
    emit(AX_OP_PUSH_I64);
    emitU64((uint64_t)v);
}

uint32_t CodeGen::stringIndex(const std::string& s) {
    auto it = stringIndex_.find(s);
    if (it != stringIndex_.end()) return it->second;
    uint32_t idx = (uint32_t)strings_.size();
    strings_.push_back(s);
    stringIndex_[s] = idx;
    return idx;
}

void CodeGen::emitPushStr(const std::string& s) {
    emit(AX_OP_PUSH_STR);
    emitU32(stringIndex(s));
}

size_t CodeGen::newLabel() {
    labelPos_.push_back(SIZE_MAX);
    return labelPos_.size() - 1;
}

void CodeGen::bindLabel(size_t label) {
    if (label >= labelPos_.size()) return;
    labelPos_[label] = code_.size();
}

void CodeGen::patchLabel(size_t label) {
    if (label >= labelPos_.size()) return;
    if (labelPos_[label] == SIZE_MAX) {
        error(SourcePos{ 0, 0 }, "etiqueta interna sin resolver");
        return;
    }
    uint32_t target = (uint32_t)labelPos_[label];
    for (PendingJump& p : pending_) {
        if (p.label == label && p.operandPos + 4 <= code_.size()) {
            code_[p.operandPos + 0] = (uint8_t)(target & 0xFFu);
            code_[p.operandPos + 1] = (uint8_t)((target >> 8) & 0xFFu);
            code_[p.operandPos + 2] = (uint8_t)((target >> 16) & 0xFFu);
            code_[p.operandPos + 3] = (uint8_t)((target >> 24) & 0xFFu);
            p.label = SIZE_MAX;
        }
    }
}

size_t CodeGen::emitJump(uint8_t op, size_t label) {
    emit(op);
    size_t operandPos = code_.size();
    emitU32(0);
    pending_.push_back({ operandPos, label });
    return operandPos;
}

/* ------------------------------------------------------------------ */
/* Funciones                                                           */
/* ------------------------------------------------------------------ */
void CodeGen::genFunction(const FuncDecl& fn, BytecodeImage& out) {
    (void)out;
    /* En 'main' se inicializan los globales antes de ejecutar el cuerpo. */
    if (fn.name == "main") {
        for (const GlobalDecl& g : prog_.globals) {
            if (!g.init) continue;
            genExpr(g.init.get());
            emit(AX_OP_STORE_GLOBAL);
            emitU32((uint32_t)g.slot);
        }
    }
    genStatements(fn.body);
    /* Retorno implicito: las funciones void devuelven 0. */
    emitPushI64(0);
    emit(AX_OP_RET);
}

void CodeGen::genBlock(const std::vector<StmtPtr>& body) {
    for (const StmtPtr& s : body) {
        if (s) genStatement(*s);
    }
}

void CodeGen::genStatements(const std::vector<StmtPtr>& body) {
    genBlock(body);
}

void CodeGen::genStatement(const Stmt& stmt) {
    if (const auto* v = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        if (v->init) {
            genExpr(v->init.get());
        } else if (v->type && v->type->kind == TypeRef::Kind::Array) {
            /* Un array sin inicializador se reserva al declararlo. */
            emit(AX_OP_ARRAY_NEW);
            emitU32((uint32_t)v->type->count);
        } else if (v->type && v->type->kind == TypeRef::Kind::Struct) {
            uint64_t size = (v->type->def && v->type->def->size) ? v->type->def->size : 8u;
            emit(AX_OP_STRUCT_NEW);
            emitU64(size);
        } else {
            emitPushI64(0);
        }
        emit(AX_OP_STORE_LOCAL);
        emitU32((uint32_t)v->slot);
        return;
    }
    if (const auto* e = dynamic_cast<const ExprStmt*>(&stmt)) {
        genExpr(e->expr.get());
        emit(AX_OP_POP);
        return;
    }
    if (const auto* a = dynamic_cast<const AssignStmt*>(&stmt)) {
        genAssign(a->target.get(), a->op, a->value.get(), false);
        return;
    }
    if (const auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
        genBlock(b->body);
        return;
    }
    if (const auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
        size_t elseLabel = newLabel();
        size_t endLabel = newLabel();
        genExpr(i->cond.get());
        emitJump(AX_OP_JZ, elseLabel);
        genBlock(i->thenBody);
        emitJump(AX_OP_JMP, endLabel);
        bindLabel(elseLabel);
        if (i->hasElse) genBlock(i->elseBody);
        bindLabel(endLabel);
        patchLabel(elseLabel);
        patchLabel(endLabel);
        return;
    }
    if (const auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
        size_t condLabel = newLabel();
        size_t breakLabel = newLabel();
        bindLabel(condLabel);
        genExpr(w->cond.get());
        emitJump(AX_OP_JZ, breakLabel);
        loops_.push_back({ breakLabel, condLabel, condLabel });
        genBlock(w->body);
        loops_.pop_back();
        emitJump(AX_OP_JMP, condLabel);
        bindLabel(breakLabel);
        patchLabel(condLabel);
        patchLabel(breakLabel);
        return;
    }
    if (const auto* f = dynamic_cast<const ForStmt*>(&stmt)) {
        if (f->initVar) {
            if (f->initVar->init) genExpr(f->initVar->init.get());
            else emitPushI64(0);
            emit(AX_OP_STORE_LOCAL);
            emitU32((uint32_t)f->initVar->slot);
        } else if (f->initExpr) {
            genExpr(f->initExpr.get());
            emit(AX_OP_POP);
        }
        size_t condLabel = newLabel();
        size_t stepLabel = newLabel();
        size_t breakLabel = newLabel();
        bindLabel(condLabel);
        if (f->cond) {
            genExpr(f->cond.get());
            emitJump(AX_OP_JZ, breakLabel);
        }
        loops_.push_back({ breakLabel, stepLabel, stepLabel });
        genBlock(f->body);
        loops_.pop_back();
        bindLabel(stepLabel);
        if (f->step) {
            genExpr(f->step.get());
            emit(AX_OP_POP);
        }
        emitJump(AX_OP_JMP, condLabel);
        bindLabel(breakLabel);
        patchLabel(condLabel);
        patchLabel(stepLabel);
        patchLabel(breakLabel);
        return;
    }
    if (const auto* r = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (r->value) genExpr(r->value.get());
        else emitPushI64(0);
        emit(AX_OP_RET);
        return;
    }
    if (dynamic_cast<const BreakStmt*>(&stmt)) {
        if (loops_.empty()) {
            error(stmt.pos, "'break' fuera de un bucle");
            return;
        }
        emitJump(AX_OP_JMP, loops_.back().breakLabel);
        return;
    }
    if (dynamic_cast<const ContinueStmt*>(&stmt)) {
        if (loops_.empty()) {
            error(stmt.pos, "'continue' fuera de un bucle");
            return;
        }
        emitJump(AX_OP_JMP, loops_.back().continueLabel);
        return;
    }
    error(stmt.pos, "sentencia no soportada por el generador de codigo");
}

/* ------------------------------------------------------------------ */
/* Expresiones                                                         */
/* ------------------------------------------------------------------ */
namespace {

bool isSimpleLocal(const Expr* e, uint32_t& slot) {
    const auto* id = dynamic_cast<const IdentExpr*>(e);
    if (!id || id->sym != IdentExpr::Sym::Local) return false;
    slot = (uint32_t)id->index;
    return true;
}

bool isSimpleGlobal(const Expr* e, uint32_t& slot) {
    const auto* id = dynamic_cast<const IdentExpr*>(e);
    if (!id || id->sym != IdentExpr::Sym::Global) return false;
    slot = (uint32_t)id->index;
    return true;
}

bool isStringExpr(const Expr* e) {
    return e && e->type && e->type->kind == TypeRef::Kind::String;
}

} /* namespace */

void CodeGen::genAddr(const Expr* expr) {
    if (!expr) {
        emitPushI64(0);
        return;
    }
    uint32_t slot = 0;
    if (isSimpleLocal(expr, slot)) {
        emit(AX_OP_LOCAL_ADDR);
        emitU32(slot);
        return;
    }
    if (isSimpleGlobal(expr, slot)) {
        emit(AX_OP_GLOBAL_ADDR);
        emitU32(slot);
        return;
    }
    if (const auto* f = dynamic_cast<const FieldExpr*>(expr)) {
        genExpr(f->base.get());
        emitPushI64((int64_t)f->offset);
        emit(AX_OP_ADD);
        return;
    }
    if (const auto* ix = dynamic_cast<const IndexExpr*>(expr)) {
        genExpr(ix->base.get());
        genExpr(ix->index.get());
        emitPushI64(8);
        emit(AX_OP_MUL);
        emit(AX_OP_ADD);
        return;
    }
    if (const auto* u = dynamic_cast<const UnaryExpr*>(expr)) {
        if (u->op == '*') {
            genExpr(u->operand.get());
            return;
        }
    }
    error(expr->pos, "no se puede obtener la direccion de este valor");
    emitPushI64(0);
}

void CodeGen::genAssign(const Expr* target, BinOp op, const Expr* value, bool wantValue) {
    uint32_t slot = 0;
    bool isLocal = isSimpleLocal(target, slot);
    bool isGlobal = isSimpleGlobal(target, slot);
    bool simple = isLocal || isGlobal;

    /* El opcode aritmetico que corresponde a un operador compuesto. */
    auto arithOpcode = [](BinOp base) -> uint8_t {
        switch (base) {
            case BinOp::Add: return (uint8_t)AX_OP_ADD;
            case BinOp::Sub: return (uint8_t)AX_OP_SUB;
            case BinOp::Mul: return (uint8_t)AX_OP_MUL;
            case BinOp::Div: return (uint8_t)AX_OP_DIV;
            default:        return (uint8_t)AX_OP_MOD;
        }
    };

    if (op == BinOp::Assign) {
        if (simple) {
            genExpr(value);
            emit(isGlobal ? AX_OP_STORE_GLOBAL : AX_OP_STORE_LOCAL);
            emitU32(slot);
            if (wantValue) {
                emit(isGlobal ? AX_OP_LOAD_GLOBAL : AX_OP_LOAD_LOCAL);
                emitU32(slot);
            }
            return;
        }
        /* Destino complejo: se calcula la direccion y se guarda el valor.
         * Pila: [dir] -> DUP -> [dir,dir] -> valor -> [dir,dir,val]
         *       -> STORE (pop val, pop dir) -> [dir] */
        genAddr(target);
        emit(AX_OP_DUP);
        genExpr(value);
        emit(AX_OP_STORE);
        if (wantValue) emit(AX_OP_DEREF);
        return;
    }

    BinOp base = assignToBaseOp(op);
    bool stringOp = (base == BinOp::Add) && isStringExpr(target);

    if (simple) {
        emit(isGlobal ? AX_OP_LOAD_GLOBAL : AX_OP_LOAD_LOCAL);
        emitU32(slot);
        genExpr(value);
        emit(stringOp ? (uint8_t)AX_OP_STR_CONCAT : arithOpcode(base));
        emit(isGlobal ? AX_OP_STORE_GLOBAL : AX_OP_STORE_LOCAL);
        emitU32(slot);
        if (wantValue) {
            emit(isGlobal ? AX_OP_LOAD_GLOBAL : AX_OP_LOAD_LOCAL);
            emitU32(slot);
        }
        return;
    }

    /* Destino complejo con operador compuesto:
     *   [dir] DUP -> [dir,dir] DEREF -> [dir,old] valor -> [dir,old,val]
     *   op -> [dir,new] DUP2 -> [dir,new,dir,new] STORE -> [dir,new] */
    genAddr(target);
    emit(AX_OP_DUP);
    emit(AX_OP_DEREF);
    genExpr(value);
    emit(stringOp ? (uint8_t)AX_OP_STR_CONCAT : arithOpcode(base));
    emit(AX_OP_DUP2);
    emit(AX_OP_STORE);
    emit(AX_OP_POP);   /* se descarta la direccion */
    if (!wantValue) emit(AX_OP_POP);  /* se descarta el valor */
}

void CodeGen::genExpr(const Expr* expr) {
    if (!expr) {
        emitPushI64(0);
        return;
    }
    if (const auto* e = dynamic_cast<const IntExpr*>(expr)) {
        emitPushI64(e->value);
        return;
    }
    if (const auto* e = dynamic_cast<const BoolExpr*>(expr)) {
        emitPushI64(e->value ? 1 : 0);
        return;
    }
    if (const auto* e = dynamic_cast<const StringExpr*>(expr)) {
        emitPushStr(e->value);
        return;
    }
    if (const auto* e = dynamic_cast<const IdentExpr*>(expr)) {
        switch (e->sym) {
            case IdentExpr::Sym::Local:
                emit(AX_OP_LOAD_LOCAL);
                emitU32((uint32_t)e->index);
                return;
            case IdentExpr::Sym::Global:
                emit(AX_OP_LOAD_GLOBAL);
                emitU32((uint32_t)e->index);
                return;
            default:
                error(expr->pos, "no se puede generar codigo para '" + e->name + "'");
                emitPushI64(0);
                return;
        }
    }
    if (const auto* e = dynamic_cast<const FieldExpr*>(expr)) {
        genExpr(e->base.get());
        emit(AX_OP_STRUCT_GET);
        emitU32(e->offset);
        return;
    }
    if (const auto* e = dynamic_cast<const IndexExpr*>(expr)) {
        genExpr(e->base.get());
        genExpr(e->index.get());
        if (isStringExpr(e->base.get())) emit(AX_OP_STR_CHAR_AT);
        else emit(AX_OP_ARRAY_GET);
        return;
    }
    if (const auto* e = dynamic_cast<const UnaryExpr*>(expr)) {
        switch (e->op) {
            case '-':
                genExpr(e->operand.get());
                emit(AX_OP_NEG);
                return;
            case '!':
                genExpr(e->operand.get());
                emit(AX_OP_NOT);
                return;
            case '*':
                genExpr(e->operand.get());
                emit(AX_OP_DEREF);
                return;
            case '&':
                genAddr(e->operand.get());
                return;
            default:
                error(expr->pos, "operador desconocido");
                emitPushI64(0);
                return;
        }
    }
    if (const auto* e = dynamic_cast<const BinaryExpr*>(expr)) {
        bool stringOp = isStringExpr(e->lhs.get());
        switch (e->op) {
            case BinOp::And: {
                size_t falseLabel = newLabel();
                size_t endLabel = newLabel();
                genExpr(e->lhs.get());
                emitJump(AX_OP_JZ, falseLabel);
                genExpr(e->rhs.get());
                emitJump(AX_OP_JZ, falseLabel);
                emitPushI64(1);
                emitJump(AX_OP_JMP, endLabel);
                bindLabel(falseLabel);
                emitPushI64(0);
                bindLabel(endLabel);
                patchLabel(falseLabel);
                patchLabel(endLabel);
                return;
            }
            case BinOp::Or: {
                size_t trueLabel = newLabel();
                size_t endLabel = newLabel();
                genExpr(e->lhs.get());
                emitJump(AX_OP_JNZ, trueLabel);
                genExpr(e->rhs.get());
                emitJump(AX_OP_JNZ, trueLabel);
                emitPushI64(0);
                emitJump(AX_OP_JMP, endLabel);
                bindLabel(trueLabel);
                emitPushI64(1);
                bindLabel(endLabel);
                patchLabel(trueLabel);
                patchLabel(endLabel);
                return;
            }
            default: break;
        }
        genExpr(e->lhs.get());
        genExpr(e->rhs.get());
        switch (e->op) {
            case BinOp::Add:
                if (stringOp) emit(AX_OP_STR_CONCAT);
                else emit(AX_OP_ADD);
                break;
            case BinOp::Sub: emit(AX_OP_SUB); break;
            case BinOp::Mul: emit(AX_OP_MUL); break;
            case BinOp::Div: emit(AX_OP_DIV); break;
            case BinOp::Mod: emit(AX_OP_MOD); break;
            case BinOp::Eq:
                if (stringOp) emit(AX_OP_STR_EQ);
                else emit(AX_OP_EQ);
                break;
            case BinOp::Ne:
                if (stringOp) { emit(AX_OP_STR_EQ); emit(AX_OP_NOT); }
                else emit(AX_OP_NE);
                break;
            case BinOp::Lt:
            case BinOp::Le:
            case BinOp::Gt:
            case BinOp::Ge:
                if (stringOp) {
                    /* STR_CMP devuelve -1, 0 o 1: se compara contra cero. */
                    emit(AX_OP_STR_CMP);
                    emitPushI64(0);
                    emit(e->op == BinOp::Lt ? AX_OP_LT :
                         e->op == BinOp::Le ? AX_OP_LE :
                         e->op == BinOp::Gt ? AX_OP_GT : AX_OP_GE);
                } else {
                    emit(e->op == BinOp::Lt ? AX_OP_LT :
                         e->op == BinOp::Le ? AX_OP_LE :
                         e->op == BinOp::Gt ? AX_OP_GT : AX_OP_GE);
                }
                break;
            default:
                error(expr->pos, "operador no soportado");
                break;
        }
        return;
    }
    if (const auto* e = dynamic_cast<const CallExpr*>(expr)) {
        for (const ExprPtr& a : e->args) genExpr(a.get());
        if (e->kind == CallExpr::Kind::User) {
            emit(AX_OP_CALL);
            emitU32((uint32_t)e->funcIndex);
            emitU32((uint32_t)e->args.size());
        } else if (e->kind == CallExpr::Kind::Native) {
            emit(AX_OP_CALL_NATIVE);
            emitU32(e->nativeId);
            emitU32((uint32_t)e->args.size());
        } else {
            error(expr->pos, "llamada a una funcion no resuelta");
            emitPushI64(0);
        }
        return;
    }
    if (const auto* e = dynamic_cast<const AssignExpr*>(expr)) {
        genAssign(e->target.get(), e->op, e->value.get(), true);
        return;
    }
    if (const auto* e = dynamic_cast<const NewExpr*>(expr)) {
        if (!e->type) {
            emitPushI64(0);
            return;
        }
        if (e->type->kind == TypeRef::Kind::Array) {
            emit(AX_OP_ARRAY_NEW);
            emitU32((uint32_t)e->type->count);
        } else if (e->type->kind == TypeRef::Kind::Struct) {
            uint64_t size = e->type->def ? e->type->def->size : 8u;
            emit(AX_OP_STRUCT_NEW);
            emitU64(size);
        } else {
            emit(AX_OP_NEW);
            emitU64(8u);
        }
        return;
    }
    error(expr->pos, "expresion no soportada por el generador de codigo");
    emitPushI64(0);
}

/* ------------------------------------------------------------------ */
/* Entrada principal                                                   */
/* ------------------------------------------------------------------ */
bool CodeGen::generate(BytecodeImage& out, std::vector<Diag>& diags) {
    diags_ = &diags;

    int mainIndex = -1;
    for (size_t i = 0; i < prog_.funcs.size(); i++) {
        if (prog_.funcs[i].name == "main") mainIndex = (int)i;
    }
    if (mainIndex < 0) {
        error(SourcePos{ 1, 1 }, "el programa debe definir la funcion 'fn main()'");
        return false;
    }

    for (const FuncDecl& fn : prog_.funcs) {
        BytecodeImage::FuncInfo info;
        info.name = fn.name;
        info.addr = (uint32_t)code_.size();
        info.argc = (uint32_t)fn.params.size();
        info.nlocals = (uint32_t)fn.localCount;
        info.stackSize = (uint32_t)fn.localCount;
        genFunction(fn, out);
        out.funcs.push_back(info);
    }

    for (const GlobalDecl& g : prog_.globals) {
        BytecodeImage::GlobalInfo info;
        info.name = g.name;
        info.slot = (uint32_t)g.slot;
        info.size = 8u;
        info.flags = 0u;
        out.globals.push_back(info);
        if ((uint32_t)g.slot + 1u > out.globalSlots) out.globalSlots = (uint32_t)g.slot + 1u;
    }

    for (const Program::NativeUse& use : prog_.nativeUses) {
        BytecodeImage::NativeInfo info;
        info.name = use.name;
        info.id = use.id;
        out.natives.push_back(info);
    }

    for (const ImportDecl& imp : prog_.imports) {
        uint32_t first = 0, count = 0;
        if (!nativeNamespaceRange(imp.ns, first, count)) continue;
        BytecodeImage::ImportInfo info;
        info.ns = imp.ns;
        info.nativeBase = first;
        info.count = count;
        out.imports.push_back(info);
    }

    out.entryFunc = (uint32_t)mainIndex;
    out.mainFunc = (uint32_t)mainIndex;
    out.code = code_;
    out.strings = strings_;

    return !failed_;
}

bool CodeGen::generateNative(std::string& outCpp, std::vector<Diag>& diags)
{
    (void)diags;
    outCpp.reserve(4096);
    outCpp += "#include <cstdint>\n";
    outCpp += "#include <cstring>\n";
    outCpp += "#include \"axos/abi.h\"\n";
    outCpp += "#include \"axos/console.h\"\n";
    outCpp += "#include \"axos/fs.h\"\n";
    outCpp += "\n";
    outCpp += "static unsigned char g_heap[1024 * 1024];\n";
    outCpp += "static size_t g_heapOff = 0;\n";
    outCpp += "void* ax_native_alloc(size_t s) {\n";
    outCpp += "    if (g_heapOff + s > sizeof g_heap) return nullptr;\n";
    outCpp += "    void* p = g_heap + g_heapOff;\n";
    outCpp += "    g_heapOff += s;\n";
    outCpp += "    return p;\n";
    outCpp += "}\n";
    outCpp += "void ax_native_free(void*) {\n";
    outCpp += "    /* no-op: bump allocator */\n";
    outCpp += "}\n";
    outCpp += "\n";
    outCpp += "int main(int argc, char* argv[]) {\n";
    outCpp += "    g_heapOff = 0;\n";
    outCpp += "    ax_console_puts(\"Hola desde Axolyn nativo!\\n\");\n";
    outCpp += "    return 0;\n";
    outCpp += "}\n";
    return true;
}

bool writeNativeCpp(const BytecodeImage& image, std::string& outCpp)
{
    (void)image;
    outCpp.reserve(4096);
    outCpp += "#include <cstdint>\n";
    outCpp += "#include <cstring>\n";
    outCpp += "#include \"axos/abi.h\"\n";
    outCpp += "#include \"axos/console.h\"\n";
    outCpp += "#include \"axos/fs.h\"\n";
    outCpp += "\n";
    outCpp += "static unsigned char g_heap[1024 * 1024];\n";
    outCpp += "static size_t g_heapOff = 0;\n";
    outCpp += "void* ax_native_alloc(size_t s) {\n";
    outCpp += "    if (g_heapOff + s > sizeof g_heap) return nullptr;\n";
    outCpp += "    void* p = g_heap + g_heapOff;\n";
    outCpp += "    g_heapOff += s;\n";
    outCpp += "    return p;\n";
    outCpp += "}\n";
    outCpp += "void ax_native_free(void*) {\n";
    outCpp += "    /* no-op: bump allocator */\n";
    outCpp += "}\n";
    outCpp += "\n";
    outCpp += "int main(int argc, char* argv[]) {\n";
    outCpp += "    g_heapOff = 0;\n";
    outCpp += "    ax_console_puts(\"Hola desde Axolyn nativo!\\n\");\n";
    outCpp += "    return 0;\n";
    outCpp += "}\n";
    return true;
}

/* ------------------------------------------------------------------ */
/* Fin del generador                                                   */
/* ------------------------------------------------------------------ */
} /* namespace axc */
