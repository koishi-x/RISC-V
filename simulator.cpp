//
// QWQ
//
#include <iostream>
#include <cstdio>
#include <queue>

//#define DEBUG
unsigned FUCK_vj[32], FUCK_vk[32];

using namespace std;
constexpr unsigned FULL_BIT = -1;
constexpr unsigned MAX_REGISTER_NUM = 32;
constexpr unsigned MAX_BUFFER_SIZE = 32;

inline void CHECK_YOUR_XXX (int x = 0) {printf("Something went wrong at %d\n", x); }
inline unsigned getBit (unsigned x, int low, int high) {return (x >> low) & (FULL_BIT >> (31 - high + low)); }
inline unsigned charToInt(char c) {return c <= '9' ? c - '0' : c - 'A' + 10; }
inline unsigned sext(unsigned x, int high) {return (x >> high) & 1 ? x | (FULL_BIT << (high + 1)) : x; }

unsigned CLOCK = 0;
bool flagEnd, flagEnd_new;
bool predictFailFlag, predictFailFlag_new;
unsigned pc, pc_new, pc_fact;
unsigned reg[MAX_REGISTER_NUM], reg_new[MAX_REGISTER_NUM];
int RF[MAX_REGISTER_NUM], RF_new[MAX_REGISTER_NUM];
unsigned FZYC[1<<12], FZYC_new[1<<12], FZYC_modify_id;
bool isRFModifiedByIssue[MAX_BUFFER_SIZE];

class Memory {
    static const int MAX_MEMORY = 500010;
    unsigned store[MAX_MEMORY];
public:
    void modify(unsigned value, int addr, int siz) {
#ifdef DEBUG
        if (9 >= addr && 9 <= addr + siz - 1) {
            cout << "Bad pc and clock: " << hex << pc <<dec<<' '<< CLOCK <<endl;
            cout << value << ' ' << addr << ' ' <<siz << endl << endl;
        }
#endif
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

string orderString[] = {"LUI","AUIPC",
                        "JAL",
                        "JALR","LB","LH","LW","LBU","LHU","ADDI","SLTI","SLTIU","XORI","ORI","ANDI","SLLI","SRLI","SRAI",
                        "BEQ","BNE","BLT","BGE","BLTU","BGEU",
                        "SB","SH","SW",
                        "ADD","SUB","SLL","SLT","SLTU","XOR","SRL","SRA","OR","AND",
                        "END"};
bool isUType(orderType x) {return x >= LUI && x <= AUIPC; }
bool isJType(orderType x) {return x == JAL;}
bool isIType(orderType x) {return x >= JALR && x <= SRAI; }
bool isBType(orderType x) {return x >= BEQ && x <= BGEU; }
bool isSType(orderType x) {return x >= SB && x <= SW; }
bool isRType(orderType x) {return x >= ADD && x <= AND; }

bool isBranch(orderType x) {return x == JAL || x == JALR || isBType(x); }
bool isLoad(orderType x) {return x >= LB && x <= LHU; }
bool isStore(orderType x) {return isSType(x); }
bool isSL(orderType x) {return isLoad(x) || isStore(x); }
bool hasRD(orderType x) {return isUType(x) || isJType(x) || isIType(x) || isRType(x); }
bool hasRS1(orderType x) {return !(isUType(x) || isJType(x)); }
bool hasRS2(orderType x) {return isBType(x) || isSType(x) || isRType(x);}

class Instruction {
public:
    unsigned rs1, rs2, rd;
    unsigned imm;
    orderType type;

    //only used for branch
    unsigned pc, next_pc;
    bool predict;
    Instruction() {}

    bool init(unsigned x) {
        if (x == 0x0ff00513) {
            type = END;
            return true;
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
                else CHECK_YOUR_XXX(1);
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
                        else CHECK_YOUR_XXX(2);
                    }
                } else {    //ADDI, SLTI, SLTIU, XORI, ORI, ANDI
                    imm = sext(getBit(x, 20, 31), 11);

                    if (detailType == 0b000) type = ADDI;
                    else if (detailType == 0b010) type = SLTI;
                    else if (detailType == 0b011) type = SLTIU;
                    else if (detailType == 0b100) type = XORI;
                    else if (detailType == 0b110) type = ORI;
                    else if (detailType == 0b111) type = ANDI;
                    else CHECK_YOUR_XXX(3);
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
                else CHECK_YOUR_XXX(4);
                break;

            case 0b0100011: //SB, SH, SW
                imm = sext(getBit(x, 7, 11) | (getBit(x, 25, 31) << 5), 11);
                detailType = getBit(x, 12, 14);
                if (detailType == 0b000) type = SB;
                else if (detailType == 0b001) type = SH;
                else if (detailType == 0b010) type = SW;
                else CHECK_YOUR_XXX(5);
                break;
            case 0b0110011: //
                detailType = getBit(x, 25, 31);

                if (detailType == 0b0100000) {
                    detailType = getBit(x, 12, 14);
                    if (detailType == 0b000) type = SUB;
                    else if (detailType == 0b101) type = SRA;
                    else CHECK_YOUR_XXX(6);
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
                    else CHECK_YOUR_XXX(7);
                } else CHECK_YOUR_XXX(8);
                break;
            default:
                return false;
                CHECK_YOUR_XXX(114514);
                std::cout << x <<' '<<pc<<' '<<CLOCK<< std::endl;
        }
        return true;
    }
    void init(const Instruction &obj) {
        rs1 = obj.rs1, rs2 = obj.rs2, rd = obj.rd;
        imm = obj.imm;
        type = obj.type;
        pc = obj.pc, next_pc = obj.next_pc;
        predict = obj.predict;
    }
    void clear() {}
    void printInfo() {
        cout << "Type: " << orderString[type] << endl;
        cout << "pc: 0x" << hex << pc << dec << endl;
        if (hasRD(type)) cout << "rd: " << rd << endl;
        if (hasRS1(type)) cout << "rs1: " << rs1 << endl;
        if (hasRS2(type)) cout << "rs2: " << rs2 << endl;
        cout << "imm: " << imm << endl;
    }
};

class ROB_node: public Instruction {
public:
    bool ready{0};

    //information of result
    unsigned value;

    //only used for branch
    bool fact;

    void clear() {
        ready = false;
    }

};

class RS_node: public Instruction {
public:
    bool busy{0};

    int qj{-1}, qk{-1};
    unsigned vj, vk, ROB_id;
    unsigned value;
};

class SL_node: public Instruction {
public:
    bool ready{0};   //"ready" is only used in store

    int qj{-1}, qk{-1};
    unsigned vj, vk, ROB_id;
    unsigned value;
    void clear() {
        ready = 0;
        qj = qk = -1;
    }
};


template<class T>
class Buffer {  //actually a queue.

    T buffer[MAX_BUFFER_SIZE];
    int head, tail, siz;
public:
    //friend void CBD_update(unsigned, unsigned);
    //friend void work_RS();
    Buffer(): head(1), tail(0), siz(0) {}

    T& operator[](unsigned index) {
        return buffer[index];
    }
    T front() {
        return buffer[head];
    }
    int front_id() {
        return head;
    }
    int back_id() {
        return tail;
    }
    void push(const T &obj) {
        if (++tail == MAX_BUFFER_SIZE) tail = 0;
        buffer[tail] = obj;
        ++siz;
    }

    void pop() {
        //buffer[head].clear();
        if (++head == MAX_BUFFER_SIZE) head = 0;
        --siz;
    }

    bool empty() {
        return siz == 0;
    }

    bool full() {
        return siz == MAX_BUFFER_SIZE;
    }
    void clear() {
        head = 1, tail = 0, siz = 0;
    }
    Buffer<T> &operator=(const Buffer<T> &obj) {
        if (&obj == this) return *this;
        head = obj.head;
        tail = obj.tail;
        siz = obj.siz;
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            buffer[i] = obj.buffer[i];
        }
        return *this;
    }
    int size() {return siz; }
};

class CBD_info {
public:
    unsigned ROB_id, value;
};
vector<CBD_info> CBD;

Buffer<Instruction> insQue, insQue_new;
Buffer<ROB_node> ROB, ROB_new;
Buffer<SL_node> SLB, SLB_new;
RS_node RS[MAX_BUFFER_SIZE], RS_new[MAX_BUFFER_SIZE];

bool dealDependence(unsigned reg_id, int &q, unsigned &v) {

    if (RF[reg_id] == -1) {
        q = -1;
        v = reg[reg_id];
        return true;
    } else if (!ROB_new[RF[reg_id]].ready){
        q = RF[reg_id];
        return false;
    } else {
        q = -1;
        v = ROB_new[RF[reg_id]].value;
        return true;
    }
}

void CBD_update(unsigned ROB_id, unsigned value) {
    CBD.push_back({ROB_id, value});
    /*
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
        if (SLB[i].qj == ROB_id) {
            SLB_new[i].qj = -1;
            SLB_new[i].vj = value;
        }
        if (SLB[i].qk == ROB_id) {
            SLB_new[i].qk = -1;
            SLB_new[i].vk = value;
        }
        if (RS[i].busy) {
            if (RS[i].qj == ROB_id) {
                RS_new[i].qj = -1;
                RS_new[i].vj = value;
            }
            if (RS[i].qk == ROB_id) {
                RS_new[i].qk = -1;
                RS_new[i].vk = value;
            }
        }
    }*/

}

void CBD_update_all() {
    for (auto cur: CBD) {
        unsigned ROB_id = cur.ROB_id, value = cur.value;
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            if (SLB[i].qj == ROB_id) {
                SLB[i].qj = SLB_new[i].qj = -1;
                SLB[i].vj = SLB_new[i].vj = value;
            }
            if (SLB[i].qk == ROB_id) {
                SLB[i].qk = SLB_new[i].qk = -1;
                SLB[i].vk = SLB_new[i].vk = value;
            }
            if (RS[i].busy) {
                if (RS[i].qj == ROB_id) {
                    RS[i].qj = RS_new[i].qj = -1;
                    RS[i].vj = RS_new[i].vj = value;
                }
                if (RS[i].qk == ROB_id) {
                    RS[i].qk = RS_new[i].qk = -1;
                    RS[i].vk = RS_new[i].vk = value;
                }
            }
        }
    }
    CBD.clear();
}

void initMemory() {
    char s[100];
    int addr = 0;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) RF[i] = RF_new[i] = -1;
    while (~scanf("%s", s)) {
        if (s[0] == '@') {
            addr = 0;
            for (int i = 1; s[i]; ++i) addr = ((addr << 4) | charToInt(s[i]));
        } else {
            mem.modify((charToInt(s[0]) << 4) | charToInt(s[1]), addr, 1);
            ++addr;
        }
    }
}

void readInstruction() {
    /*
    //unsigned curInstruction = mem.query(pc, 4);
    static bool curState = 0;
    static unsigned x;


    if (!curState) {
        x = mem.query(pc, 4);
        curState = true;
        return;
    }

    curState = false;
    */

    if (predictFailFlag) {
        insQue.clear();
        insQue_new.clear();
        flagEnd = false;
        //puts("Return reason: predict fail.");
        return;
    }

    Instruction curInstruction;
    unsigned x = mem.query(pc, 4);
    if (!curInstruction.init(x)) {
        //puts("Return reason: invalid operation.");
        return;
    }
    //cout << "Query: "<<hex<<x <<" at clock " << CLOCK <<" in pc " <<pc<< endl;
    if (curInstruction.type == END) {
        //puts("Return reason: end program.");
        flagEnd_new = true;
        return;
    }
    if (insQue.full()) {
     //   puts("Return reason: instruction queue full.");
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
    } else {
        pc_new = pc + 4;
    }
#ifdef DEBUG
    cout << "###############\n";
    curInstruction.printInfo();
    cout << "#################\n";
#endif
    insQue_new.push(curInstruction);
}

void issueInstruction() {

    if (predictFailFlag) {
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            RF[i] = RF_new[i] = -1;
        }
        return;
    }

    if (insQue.empty()) return;
    if (ROB.full()) return;
    Instruction cur = insQue.front();

    ROB_node tmp;
    tmp.init(cur);
    tmp.ready = false;
    if (isSL(cur.type)) {
        if (SLB.full()) return;
        ROB_new.push(tmp);
        insQue_new.pop();

        SL_node tmpSL;
        tmpSL.init(cur);
        tmpSL.ROB_id = ROB_new.back_id();
        tmpSL.ready = false;
        if (hasRS1(cur.type)) {
            dealDependence(tmpSL.rs1, tmpSL.qj, tmpSL.vj);
        } else {
            tmpSL.qj = -1;
        }
        if (hasRS2(cur.type)) {
            dealDependence(tmpSL.rs2, tmpSL.qk, tmpSL.vk);
        } else {
            tmpSL.qk = -1;
        }
        SLB_new.push(tmpSL);
        if (hasRD(cur.type)) RF_new[cur.rd] = tmpSL.ROB_id, isRFModifiedByIssue[cur.rd] = true;
    } else {
        int pos = -1;
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            if (!RS[i].busy) {
                pos = i;
                break;
            }
        }
        if (pos == -1) return;

        ROB_new.push(tmp);
        insQue_new.pop();

        RS_node tmpRS;
        tmpRS.init(cur);
        tmpRS.ROB_id = ROB_new.back_id();

        if (hasRS1(cur.type)) {
            dealDependence(tmpRS.rs1, tmpRS.qj, tmpRS.vj);
        } else {
            tmpRS.qj = -1;
        }
        if (hasRS2(cur.type)) {
            dealDependence(tmpRS.rs2, tmpRS.qk, tmpRS.vk);
        } else {
            tmpRS.qk = -1;
        }
        tmpRS.busy = true;
        RS_new[pos] = tmpRS;
        if (hasRD(cur.type)) RF_new[cur.rd] = tmpRS.ROB_id, isRFModifiedByIssue[cur.rd] = true;
    }

}

void work_RS() {

    if (predictFailFlag) {
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            RS[i].busy = RS_new[i].busy = false;
        }
        return;
    }

    int pos = -1;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
        if (RS[i].busy && RS[i].qj == -1 && RS[i].qk == -1) {
            pos = i;
            break;
        }
    }
    if (pos == -1) return;
    unsigned ROB_id = RS[pos].ROB_id;

    FUCK_vj[ROB_id] = RS[pos].vj;
    FUCK_vk[ROB_id] = RS[pos].vk;

    RS_new[pos].busy = false;
    ROB_new[ROB_id].ready = true;


    switch (RS[pos].type) {
        //branch type
        case JAL:
            ROB_new[ROB_id].fact = true;
            ROB_new[ROB_id].value = RS[pos].pc + 4;
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case JALR:
            ROB_new[ROB_id].fact = true;
            ROB_new[ROB_id].value = RS[pos].pc + 4;
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            ROB_new[ROB_id].next_pc = (RS[pos].vj + ROB[ROB_id].imm) & (~1);
            break;
        case BEQ:
            ROB_new[ROB_id].fact = (RS[pos].vj == RS[pos].vk);
            break;
        case BNE:
            ROB_new[ROB_id].fact = (RS[pos].vj != RS[pos].vk);
            break;
        case BLT:
            ROB_new[ROB_id].fact = ((int)RS[pos].vj < (int)RS[pos].vk);
            break;
        case BGE:
            ROB_new[ROB_id].fact = ((int)RS[pos].vj >= (int)RS[pos].vk);
            break;
        case BLTU:
            ROB_new[ROB_id].fact = (RS[pos].vj < RS[pos].vk);
            break;
        case BGEU:
            ROB_new[ROB_id].fact = (RS[pos].vj >= RS[pos].vk);
            break;

        //common type
        case LUI:
            ROB_new[ROB_id].value = RS[pos].imm;
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case AUIPC:
            ROB_new[ROB_id].value = RS[pos].pc + RS[pos].imm;
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case ADDI:
            ROB_new[ROB_id].value = RS[pos].vj + RS[pos].imm;
            CBD_update(ROB_id, ROB_new[ROB_id].value);
#ifdef DEBUG
            cout << "ADDI result: " << ' ' << ROB_new[ROB_id].value << ' ' << ROB_new[ROB_id].pc<<endl;
            cout << ROB_id << ' ' << RF[2] <<' ' << RF_new[2] << endl;
#endif
            break;
        case SLTI:
            ROB_new[ROB_id].value = ((int)RS[pos].vj < (int)RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SLTIU:
            ROB_new[ROB_id].value = (RS[pos].vj < RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case XORI:
            ROB_new[ROB_id].value = (RS[pos].vj ^ RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case ORI:
            ROB_new[ROB_id].value = (RS[pos].vj | RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case ANDI:
            ROB_new[ROB_id].value = (RS[pos].vj & RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SLLI:
            ROB_new[ROB_id].value = (RS[pos].vj << RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SRLI:
            ROB_new[ROB_id].value = (RS[pos].vj >> RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SRAI:
            ROB_new[ROB_id].value = ((int)RS[pos].vj >> RS[pos].imm);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case ADD:
            ROB_new[ROB_id].value = (RS[pos].vj + RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SUB:
            ROB_new[ROB_id].value = (RS[pos].vj - RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SLL:
            ROB_new[ROB_id].value = (RS[pos].vj << (RS[pos].vk & 31u));
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SLT:
            ROB_new[ROB_id].value = ((int)RS[pos].vj < (int)RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SLTU:
            ROB_new[ROB_id].value = (RS[pos].vj < RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case XOR:
            ROB_new[ROB_id].value = (RS[pos].vj ^ RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SRL:
            ROB_new[ROB_id].value = (RS[pos].vj >> (RS[pos].vk & 31u));
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case SRA:
            ROB_new[ROB_id].value = ((int)RS[pos].vj >> (RS[pos].vk & 31u));
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case OR:
            ROB_new[ROB_id].value = (RS[pos].vj | RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        case AND:
            ROB_new[ROB_id].value = (RS[pos].vj & RS[pos].vk);
            CBD_update(ROB_id, ROB_new[ROB_id].value);
            break;
        default:
            CHECK_YOUR_XXX(9);
            std::cout << RS[pos].type<<' '<<hex<<pc<<dec<<endl;
    }

    /*
    if (isBranch(RS[pos].type)) {
        switch (RS[pos].type) {
            case JAL:
                ROB_new[ROB_id].fact = true;
                ROB_new[ROB_id].value = RS[pos].pc + 4;
                CBD_update(ROB_id, RS[pos].pc + 4);
                break;
            case JALR:
                ROB_new[ROB_id].fact = false;
                ROB_new[ROB_id].value = RS[pos].pc + 4;
                CBD_update(ROB_id, RS[pos].pc + 4);
                ROB_new[ROB_id].next_pc = RS[pos].vj + ROB[ROB_id].imm;
                break;
            case BEQ:
                ROB_new[ROB_id].fact = (RS[pos].vj == RS[pos].vk);
                break;
            case BNE:
                ROB_new[ROB_id].fact = (RS[pos].vj != RS[pos].vk);
                break;
            case BLT:
                ROB_new[ROB_id].fact = ((int)RS[pos].vj < (int)RS[pos].vk);
                break;
            case BGE:
                ROB_new[ROB_id].fact = ((int)RS[pos].vj >= (int)RS[pos].vk);
                break;
            case BLTU:
                ROB_new[ROB_id].fact = (RS[pos].vj < RS[pos].vk);
                break;
            case BGEU:
                ROB_new[ROB_id].fact = (RS[pos].vj >= RS[pos].vk);
                break;
        }
    }*/

}

signed SLcycle = 0;
void work_SLB() {
    if (predictFailFlag) {
        SLB.clear();
        SLB_new.clear();
        SLcycle = 0;
        return;
    }

    if (SLcycle) {
        --SLcycle;
        if (SLcycle == 0) {
            SL_node tmp = SLB.front();
            SLB_new.pop();
            unsigned ROB_id = tmp.ROB_id;
            ROB_new[ROB_id].ready = true;

            FUCK_vj[ROB_id] = tmp.vj;
            FUCK_vk[ROB_id] = tmp.vk;
#ifdef DEBUG2
            if (CLOCK == 82 || CLOCK == 224) {
                cout << "pc: " << hex << tmp.pc << ',';
                cout << tmp.type << ' ' << tmp.vj << ' ' << tmp.vk << ' ' << tmp.imm << endl;
            }
#endif
            switch (tmp.type) {
                case LB:
                    ROB_new[ROB_id].value = sext(mem.query(tmp.vj + tmp.imm, 1), 7);
                    CBD_update(ROB_id, ROB_new[ROB_id].value);
                    break;
                case LH:
                    ROB_new[ROB_id].value = sext(mem.query(tmp.vj + tmp.imm, 2), 15);
                    CBD_update(ROB_id, ROB_new[ROB_id].value);
                    break;
                case LW:
                    ROB_new[ROB_id].value = mem.query(tmp.vj + tmp.imm, 4); //no need to sign-extend in RV32I.
                    CBD_update(ROB_id, ROB_new[ROB_id].value);
#ifdef DEBUG
                    if (tmp.pc == 0x1060) {
                        cout << "QWQ: " << ROB_new[ROB_id].value << ' ' << RF[tmp.rd] << ' ' << RF_new[tmp.rd] << ' ' << ROB_id<<endl;
                    }
#endif
                    break;
                case LBU:
                    ROB_new[ROB_id].value = mem.query(tmp.vj + tmp.imm, 1);
                    CBD_update(ROB_id, ROB_new[ROB_id].value);
                    break;
                case LHU:
                    ROB_new[ROB_id].value = mem.query(tmp.vj + tmp.imm, 2);
                    CBD_update(ROB_id, ROB_new[ROB_id].value);
                    break;
                case SB:
                    mem.modify(tmp.vk, tmp.vj + tmp.imm, 1);
                    break;
                case SH:
                    mem.modify(tmp.vk, tmp.vj + tmp.imm, 2);
                    break;
                case SW:
                    mem.modify(tmp.vk, tmp.vj + tmp.imm, 4);
                    break;
            }
        }
        return;
    }

    if (SLB.empty()) return;
    SL_node tmp = SLB.front();
    if (isLoad(tmp.type)) {
        if (tmp.qj == -1) {
            SLcycle = 3;
        }
    } else if (isStore(tmp.type)) {
        if (tmp.qj == -1 && tmp.qk == -1 && tmp.ready) {
            SLcycle = 3;
        }
    } else CHECK_YOUR_XXX(10);
}

int predictSuccess = 0, predictTot = 0;

/*
void clearAll() {
    insQue.clear(); insQue_new.clear();
    ROB.clear(); ROB_new.clear();
    SLB.clear(); SLB_new.clear();
    SLcycle = 0;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) RS[i].busy = RS_new[i].busy = false;
    flagEnd_new = false;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) RF_new[i] = -1;
    predictFailFlag_new = 0;
}
*/

void work_ROB() {
    if (predictFailFlag) {
        ROB.clear();
        ROB_new.clear();
        return;
    }
    if (ROB.empty()) return;
    auto tmp = ROB.front();
    unsigned ROB_id = ROB.front_id();

#ifdef DEBUG
    static int last_ROB_id = -1;


    if (ROB_id != last_ROB_id) {
        if (last_ROB_id != -1) {
            cout << "vj: " << FUCK_vj[last_ROB_id] << endl;
            cout << "vk: " << FUCK_vk[last_ROB_id] << endl << endl;
        }
        tmp.printInfo(), last_ROB_id = ROB_id;
    }
#endif
#ifdef DEBUG3
    static int last_ROB_id = 0;

    if (ROB_id != last_ROB_id) {
        tmp.printInfo(), last_ROB_id = ROB_id;
        cout << "RF[8]: " << RF[8] << endl;
    }
    if (ROB_id == 5) {
        exit(1);
    }
#endif
    if (!tmp.ready) {
        if (isStore(tmp.type)) {
            for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
                if (SLB[i].ROB_id == ROB_id) {
                    SLB_new[i].ready = true;
                }
            }
        }
        return;
    }
    ROB_new.pop();
    if (hasRD(tmp.type)) {
        CBD_update(ROB_id, tmp.value);
        reg_new[tmp.rd] = tmp.value;
        if (RF[tmp.rd] == ROB_id && !isRFModifiedByIssue[tmp.rd]) {
            RF_new[tmp.rd] = -1;
        }
        /*
        for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
            if (RF[i] == ROB_id) {
                RF_new[i] = 0;
                reg_new[i] = tmp.value;
            }
        }*/
    }

    if (isBranch(tmp.type)) {
        ++predictTot;
        if (tmp.predict == tmp.fact) {
            ++predictSuccess;
            FZYC_modify_id = (tmp.imm & 0xFFF);
            if (FZYC[FZYC_modify_id] < 3) ++FZYC_new[FZYC_modify_id];
        } else {
            FZYC_modify_id = (tmp.imm & 0xFFF);
            if (FZYC[FZYC_modify_id] > 0) --FZYC_new[FZYC_modify_id];
            pc_fact = tmp.fact ? tmp.next_pc : tmp.pc + 4;
            predictFailFlag_new = true;
        }
        /*
        if (tmp.type == JAL) {
            ++predictSuccess;
            FZYC_modify_id = (tmp.imm & 0xFFF);
            if (FZYC[FZYC_modify_id] < 3) ++FZYC_new[FZYC_modify_id];
        } else if (tmp.type == JALR) {
            predictFail = true;
            FZYC_modify_id = (tmp.imm & 0xFFF);
            if (FZYC_modify_id > 0) --FZYC_new[FZYC_modify_id];
        } else {
            if ()
        }*/
    }
}


void updateAll() {

    if (predictFailFlag) {
        predictFailFlag = false;
        pc = pc_new = pc_fact;
        return;
    }
    insQue = insQue_new;
    ROB = ROB_new;
    SLB = SLB_new;
    pc = pc_new;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {

        RS[i] = RS_new[i], RF[i] = RF_new[i], reg[i] = reg_new[i];
        isRFModifiedByIssue[i] = false;
    }
    RF[0] = RF_new[0] = -1, reg[0] = reg_new[0] = 0;
    flagEnd = flagEnd_new;
    flagEnd_new = false;
    predictFailFlag = predictFailFlag_new;
    predictFailFlag_new = false;
    FZYC[FZYC_modify_id] = FZYC_new[FZYC_modify_id];
    CBD_update_all();
}

int getRSSize() {
    int res = 0;
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) res += RS[i].busy;
    return res;
}

int getReadyROB() {
    int res = 0;
    for (int i = ROB.front_id();;++i) {
        if (i == MAX_BUFFER_SIZE) i = 0;
        res += ROB[i].ready;
        if (i == ROB.back_id()) break;
    }
    return res;
}

int main() {
    //freopen("testcases/tak.data", "r", stdin);
    //freopen("my.out", "w", stdout);
    initMemory();
    flagEnd = false;
    pc = 0;
    while (true) {
        /*

        if (ROB.front_id() == 7) {
             cout<<"";
                  /*
         if (ROB.front_id() == 8) {
             cout << hex<<ROB[8].pc << dec<<' ' << ROB[8].type << endl;
             int pos = -1;
             for (int i = 0; i < MAX_BUFFER_SIZE; ++i)
                 if (RS[i].ROB_id == 8) {
                     std::cout << RS[i].qj << ' ' << RS[i].qk << endl;
                 }
         }
        */
/*
        cout << "Current pc: " << hex << pc << endl;
        std::cout << "ROB size: " << dec<<ROB.size() << endl;
        cout << "Ready ROB node: " << getReadyROB() <<"/" <<ROB.front_id()<< endl;
        cout << "SLB size: " << SLB.size() << endl;
        cout << "RS size: " << getRSSize() << endl;
        cout << "ins_que size: " << insQue.size() << endl<<endl;
*/
        ++CLOCK;
        //posedge
        issueInstruction();
        work_SLB();
        work_ROB();
        work_RS();
        readInstruction();

        //negedge
        //if (predictFailFlag_new) clearAll();
        updateAll();
        if (flagEnd && insQue.empty() && ROB.empty() && !predictFailFlag) break;
    }
    std::cout << (reg[10] & 255u) << endl;
    ///cout << predictSuccess << '/' << predictTot;
}