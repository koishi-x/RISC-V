//
// QWQ
//
#include <iostream>
#include <cstdio>
#include <queue>

using namespace std;
constexpr unsigned FULL_BIT = -1;
constexpr unsigned MAX_REGISTER_NUM = 32;
constexpr unsigned MAX_BUFFER_SIZE = 32;

inline void CHECK_YOUR_XXX () {puts("Something went wrong"); }
inline unsigned getBit (unsigned x, int low, int high) {return (x >> low) & (FULL_BIT >> (31 - high + low)); }
inline unsigned charToInt(char c) {return c <= '9' ? c - '0' : c - 'A' + 10; }
inline unsigned sext(unsigned x, int high) {return (x >> high) & 1 ? x | (FULL_BIT << (high + 1)) : x; }

bool flagEnd;
unsigned pc, pc_new;
unsigned reg_old[MAX_REGISTER_NUM], reg_new[MAX_REGISTER_NUM];
unsigned RF_old[MAX_REGISTER_NUM], RF_new[MAX_REGISTER_NUM];
unsigned FZYC[1<<12], FZYC_new[1<<12], FZYC_modify_id;

class Memory {
    static const int MAX_MEMORY = 500010;
    unsigned store[MAX_MEMORY];
public:
    void modify(unsigned value, int addr, int siz) {
        if (siz == 1) {
            store[addr] = getBit(value, 0, 7);
        } else if (siz == 2) {
            store[addr] = getBit(value, 0, 7);
            store[addr + 1] = getBit(value, 8, 15);
        } else if (siz == 4) {
            store[addr] = getBit(value, 0, 7);
            store[addr + 1] = getBit(value, 8, 15);
            store[addr + 2] = getBit(value, 16, 23);
            store[addr + 3] = getBit(value, 24, 31);
        }
    }
    unsigned query(unsigned addr, int siz) {
        if (siz == 1) {
            return store[addr];
        } else if (siz == 2) {
            return (store[addr+1] << 8) | store[addr];
        } else if (siz == 4) {
            return (store[addr+3] << 24) | (store[addr+2] << 16) | (store[addr+1] << 8) | store[addr];
        }
    }
}mem;

enum orderType {
    LUI, AUIPC,     //U-type, 0-1
    JAL,            //J-type, 2
    JALR, LB, LH, LW, LBU, LHU, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,   //I-type, 3-17
    BEQ, BNE, BLT, BGE, BLTU, BGEU,     //B-type, 18-23
    SB, SH, SW,      //S-type, 24-26
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,   //R-type, 27-36
    END
};

bool isUType(orderType x) {return x >= LUI && x <= AUIPC; }
bool isJType(orderType x) {return x == JAL;}
bool isIType(orderType x) {return x >= JALR && x <= SRAI; }
bool isBType(orderType x) {return x >= BEQ && x <= BGEU; }
bool isSType(orderType x) {return x >= SB && x <= SW; }
bool isRType(orderType x) {return x >= ADD && x <= AND; }

bool isBranch(orderType x) {return x >= JAL && x <= SRAI; }

class Instruction {
public:
    unsigned rs1, rs2, rd;
    unsigned imm;
    orderType type;

    //only used for branch
    unsigned pc, next_pc;
    bool predict;
    Instruction() {}

    void init(unsigned x) {
        if (x == 0x0ff00513) {
            type = END;
            return;
        }
        int typeId, detailType;
        typeId = getBit(x, 0, 6);
        rs1 = getBit(x, 15, 19);
        rs2 = getBit(x, 20, 24);
        rd = getBit(x, 7, 11);

        switch (typeId) {
            case 0b0110111: //LUI
                imm = getBit(x, 12, 31) << 12;  //no need to sext
                type = LUI;
                break;
            case 0b0010111: //AUIPC
                imm =  getBit(x, 12, 31) << 12; //no need to sext
                type = AUIPC;
                break;
            case 0b1101111: //JAL
                imm = sext((getBit(x, 12, 19) << 12) | (getBit(x, 20, 20) << 11) | (getBit(x, 21, 30) << 1) | (getBit(x, 31, 31) << 20), 20);
                type = JAL;
                break;
            case 0b1100111: //JALR
                imm = sext(getBit(x, 20, 31), 11);
                type = JALR;
                break;
            case 0b0000011: //LB, LH, LW, LBU, LHU
                imm = sext(getBit(x, 20, 31), 11);

                detailType = getBit(x, 12, 14);
                if (detailType == 0b000) type = LB;
                else if (detailType == 0b001) type = LH;
                else if (detailType == 0b010) type = LW;
                else if (detailType == 0b100) type = LBU;
                else if (detailType == 0b101) type = LHU;
                else CHECK_YOUR_XXX();
                break;
            case 0b0010011: //ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI
                detailType = getBit(x, 12, 14);
                if (detailType == 0b001 || detailType == 0b101) {   //SLLI, SRLI, SRAI

                    imm = getBit(x, 20, 24);    //imm means shamt here.

                    if (detailType == 0b001) type = SLLI;
                    else {
                        detailType = getBit(x, 25, 31);
                        if (detailType == 0b0000000) type = SRLI;
                        else if (detailType == 0b0100000) type = SRAI;
                        else CHECK_YOUR_XXX();
                    }
                } else {    //ADDI, SLTI, SLTIU, XORI, ORI, ANDI
                    imm = sext(getBit(x, 20, 31), 11);

                    if (detailType == 0b000) type = ADDI;
                    else if (detailType == 0b010) type = SLTI;
                    else if (detailType == 0b011) type = SLTIU;
                    else if (detailType == 0b100) type = XORI;
                    else if (detailType == 0b110) type = ORI;
                    else if (detailType == 0b111) type = ANDI;
                    else CHECK_YOUR_XXX();
                }
                break;

            case 0b1100011: //BEQ, BNE, BLT, BGE, BLTU, BGEU
                detailType = getBit(x, 12, 14);
                imm = sext((getBit(x, 7, 7) << 11) | (getBit(x, 8, 11) << 1) | (getBit(x, 25, 30) << 5) | (getBit(x, 31, 31) << 12), 12);
                if (detailType == 0b000) type = BEQ;
                else if (detailType == 0b001) type = BNE;
                else if (detailType == 0b100) type = BLT;
                else if (detailType == 0b101) type = BGE;
                else if (detailType == 0b110) type = BLTU;
                else if (detailType == 0b111) type = BGEU;
                else CHECK_YOUR_XXX();
                break;

            case 0b0100011: //SB, SH, SW
                imm = sext(getBit(x, 7, 11) | (getBit(x, 25, 31) << 5), 11);
                detailType = getBit(x, 12, 14);
                if (detailType == 0b000) type = SB;
                else if (detailType == 0b001) type = SH;
                else if (detailType == 0b010) type = SW;
                else CHECK_YOUR_XXX();
                break;
            case 0b0110011: //
                detailType = getBit(x, 25, 31);

                if (detailType == 0b0100000) {
                    detailType = getBit(x, 12, 14);
                    if (detailType == 0b000) type = SUB;
                    else if (detailType == 0b101) type = SRA;
                    else CHECK_YOUR_XXX();
                } else if (detailType == 0b000000) {
                    detailType = getBit(x, 12, 14);
                    if (detailType == 0b000) type = ADD;
                    else if (detailType == 0b001) type = SLL;
                    else if (detailType == 0b010) type = SLT;
                    else if (detailType == 0b011) type = SLTU;
                    else if (detailType == 0b100) type = XOR;
                    else if (detailType == 0b101) type = SRL;
                    else if (detailType == 0b110) type = OR;
                    else if (detailType == 0b111) type = AND;
                    else CHECK_YOUR_XXX();
                } else CHECK_YOUR_XXX();
        }
    }
    void init(const Instruction &obj) {
        rs1 = obj.rs1, rs2 = obj.rs2, rd = obj.rd;
        imm = obj.imm;
        type = obj.type;
        pc = obj.pc, next_pc = obj.next_pc;
        predict = obj.predict;
    }
};

class ROB_node: public Instruction {
public:
    bool busy, ready;

    //information of result
    unsigned value;

    //only used for branch
    bool fact;

};

class RS_node: public Instruction {
public:
    bool busy;

    unsigned qj, qk, vj, vk, ROB_id;
    unsigned value, pc, next_pc;
};

class SL_node: public Instruction {
public:
    bool busy, ready;   //"ready" is only used in store

    unsigned qj, qk, vj, vk, ROB_id;
    unsigned value;
};

template<class T>
class Buffer {  //actually a queue.

    T buffer[MAX_BUFFER_SIZE];
    int head, tail, siz;
public:
    Buffer(): head(1), tail(0), siz(0) {}

    T front() {
        return buffer[head];
    }

    void push(const T &obj) {
        if (++tail == MAX_BUFFER_SIZE) tail = 0;
        buffer[tail] = obj;
        ++siz;
    }

    void pop() {
        if (++head == MAX_BUFFER_SIZE) head = 0;
        --siz;
    }

    bool empty() {
        return siz == 0;
    }

    bool full() {
        return siz == MAX_BUFFER_SIZE;
    }
};
Buffer<Instruction> ins_que;
Buffer<ROB_node> ROB;
Buffer<SL_node> SLB;
RS_node RS[MAX_BUFFER_SIZE];

void initMemory() {
    char s[100];
    int addr = 0;
    while (~scanf("%s", s)) {
        if (s[0] == '@') {
            addr = 0;
            for (int i = 1; s[i]; ++i) addr = addr * 10 + s[i] - '0';
        } else {
            mem.modify((charToInt(s[0]) << 4) | charToInt(s[1]), addr, 1);
            ++addr;
        }
    }
}

void readInstruction() {
    //unsigned curInstruction = mem.query(pc, 4);
    static bool curState = 0;
    static unsigned x;

    if (!curState) {
        x = mem.query(pc, 4);
        curState = 1;
        return;
    }
    curState = 0;

    Instruction curInstruction;
    curInstruction.init(x);

    if (curInstruction.type == END) {
        flagEnd = true;
        return;
    }
    if (ins_que.full()) {
        return;
    }
    curInstruction.pc = pc;
    if (isBranch(curInstruction.type)) {
        if (curInstruction.type == JAL) {
            pc_new = curInstruction.next_pc = pc + curInstruction.imm;
            curInstruction.predict = true;
        } else if (curInstruction.type == JALR) {
            pc_new = pc + 4;
            //next_pc cannot be calculated here.
            curInstruction.predict = false;
        } else {
            curInstruction.predict = (FZYC[curInstruction.imm & 0xFFF] > 1);
            curInstruction.next_pc = pc + curInstruction.imm;
            pc_new = curInstruction.predict ? curInstruction.next_pc : pc + 4;
        }
    }
}

void issueInstruction() {

    if (ins_que.empty()) return;
    Instruction cur = ins_que.front();
    ins_que.pop();

    ROB_node tmp;
    tmp.init(cur);
    tmp.
}

int main() {
    initMemory();
    flagEnd = false;
    pc = 0;
    while (true) {
        readInstruction();
        issueInstruction();
    }
}