#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <limits.h>

#define NUM_REGS 16
#define MAX_INSTR 500
#define NUM_PROGRAMS 100
#define LEN_ABSOLUTE  0
#define LEN_EFFECTIVE 1

typedef union reg {
    uint64_t q[2];
    uint32_t d[4];
    uint16_t wd[8];
    uint8_t  b[16];
} register_t;

typedef struct instruction {
    uint8_t opcode;
    uint8_t operands[3];
    uint8_t flags;
    void (*fp)(uint16_t src[8], uint16_t dst[8], uint8_t imm);
} instruction_t;

typedef struct program {
    int length[2];  /* 0 = absolute, 1 = effective */
    int fitness;
    int cost;
    register_t registers[NUM_REGS];
    instruction_t instructions[MAX_INSTR];
    instruction_t effective[MAX_INSTR];
} program_t;


static uint16_t levels[8*8];
static uint16_t coeffs[8*8];
// static uint8_t counter[65];

#define ZIG(i,y,x) levels[i] = coeffs[x*8+y];

void init_levels_8x8()
{
    ZIG( 0,0,0) ZIG( 1,0,1) ZIG( 2,1,0) ZIG( 3,2,0)
    ZIG( 4,1,1) ZIG( 5,0,2) ZIG( 6,0,3) ZIG( 7,1,2)
    ZIG( 8,2,1) ZIG( 9,3,0) ZIG(10,4,0) ZIG(11,3,1)
    ZIG(12,2,2) ZIG(13,1,3) ZIG(14,0,4) ZIG(15,0,5)
    ZIG(16,1,4) ZIG(17,2,3) ZIG(18,3,2) ZIG(19,4,1)
    ZIG(20,5,0) ZIG(21,6,0) ZIG(22,5,1) ZIG(23,4,2)
    ZIG(24,3,3) ZIG(25,2,4) ZIG(26,1,5) ZIG(27,0,6)
    ZIG(28,0,7) ZIG(29,1,6) ZIG(30,2,5) ZIG(31,3,4)
    ZIG(32,4,3) ZIG(33,5,2) ZIG(34,6,1) ZIG(35,7,0)
    ZIG(36,7,1) ZIG(37,6,2) ZIG(38,5,3) ZIG(39,4,4)
    ZIG(40,3,5) ZIG(41,2,6) ZIG(42,1,7) ZIG(43,2,7)
    ZIG(44,3,6) ZIG(45,4,5) ZIG(46,5,4) ZIG(47,6,3)
    ZIG(48,7,2) ZIG(49,7,3) ZIG(50,6,4) ZIG(51,5,5)
    ZIG(52,4,6) ZIG(53,3,7) ZIG(54,4,7) ZIG(55,5,6)
    ZIG(56,6,5) ZIG(57,7,4) ZIG(58,7,5) ZIG(59,6,6)
    ZIG(60,5,7) ZIG(61,6,7) ZIG(62,7,6) ZIG(63,7,7)
}

#undef ZIG
#define ZIG(i,y,x) levels[i] = coeffs[x*4+y];

void init_levels_4x4()
{
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 4; j++)
            coeffs[i*4+j] = rand() % UINT16_MAX;
    ZIG( 0,0,0) ZIG( 1,0,1) ZIG( 2,1,0) ZIG( 3,2,0)
    ZIG( 4,1,1) ZIG( 5,0,2) ZIG( 6,0,3) ZIG( 7,1,2)
    ZIG( 8,2,1) ZIG( 9,3,0) ZIG(10,3,1) ZIG(11,2,2)
    ZIG(12,1,3) ZIG(13,2,3) ZIG(14,3,2) ZIG(15,3,3)
}

enum instructions {
    PUNPCKLWD   = 0,
    PUNPCKHWD,
    PUNCPKLDQ,
    PUNPCKHDQ,
    PUNPCKLQDQ,
    PUNPCKHQDQ,
    MOVDQA,
    PSLLDQ,
    PSRLDQ,
    PSLLQ,
    PSRLQ,
    PSLLD,
    PSRLD,
    PSHUFLW,
    PSHUFHW,
    NUM_INSTR
};


static const uint8_t allowedshuf[24] = { (0<<6)+(1<<4)+(2<<2)+(3<<0),
                                         (0<<6)+(1<<4)+(3<<2)+(2<<0),
                                         (0<<6)+(2<<4)+(3<<2)+(1<<0),
                                         (0<<6)+(2<<4)+(1<<2)+(3<<0),
                                         (0<<6)+(3<<4)+(2<<2)+(1<<0),
                                         (0<<6)+(3<<4)+(1<<2)+(2<<0),

                                         (1<<6)+(0<<4)+(2<<2)+(3<<0),
                                         (1<<6)+(0<<4)+(3<<2)+(2<<0),
                                         (1<<6)+(2<<4)+(3<<2)+(0<<0),
                                         (1<<6)+(2<<4)+(0<<2)+(3<<0),
                                         (1<<6)+(3<<4)+(2<<2)+(0<<0),
                                         (1<<6)+(3<<4)+(0<<2)+(2<<0),

                                         (2<<6)+(1<<4)+(0<<2)+(3<<0),
                                         (2<<6)+(1<<4)+(3<<2)+(0<<0),
                                         (2<<6)+(0<<4)+(3<<2)+(1<<0),
                                         (2<<6)+(0<<4)+(1<<2)+(3<<0),
                                         (2<<6)+(3<<4)+(0<<2)+(1<<0),
                                         (2<<6)+(3<<4)+(1<<2)+(0<<0),

                                         (3<<6)+(1<<4)+(2<<2)+(0<<0),
                                         (3<<6)+(1<<4)+(0<<2)+(2<<0),
                                         (3<<6)+(2<<4)+(0<<2)+(1<<0),
                                         (3<<6)+(2<<4)+(1<<2)+(0<<0),
                                         (3<<6)+(0<<4)+(2<<2)+(1<<0),
                                         (3<<6)+(0<<4)+(1<<2)+(2<<0)};

void print_instruction( instruction_t *instr, int debug )
{
    switch( instr->opcode ) {
        case PUNPCKLWD:
            printf( "punpcklwd" );
            break;
        case PUNPCKHWD:
            printf( "punpckhwd" );
            break;
        case PUNCPKLDQ:
            printf( "punpckldq" );
            break;
        case PUNPCKHDQ:
            printf( "punpckhdq" );
            break;
        case PUNPCKLQDQ:
            printf( "punpcklqdq" );
            break;
        case PUNPCKHQDQ:
            printf( "punpckhqdq" );
            break;
        case MOVDQA:
            printf( "movdqa" );
            break;
        case PSLLDQ:
            printf( "pslldq" );
            break;
        case PSRLDQ:
            printf( "psrldq" );
            break;
        case PSLLQ:
            printf( "psllq" );
            break;
        case PSRLQ:
            printf( "psrlq" );
            break;
        case PSLLD:
            printf( "pslld" );
            break;
        case PSRLD:
            printf( "psrld" );
            break;
        case PSHUFLW:
            printf( "pshuflw" );
            break;
        case PSHUFHW:
            printf( "pshufhw" );
            break;
        default:
            fprintf( stderr, "Error: unsupported instruction!\n");
            assert(0);
    }
    if(instr->opcode < PSLLDQ ) {
                                        printf(" m%d, ", instr->operands[0]);
        if (instr->operands[1] < NUM_REGS)
                                        printf("m%d", instr->operands[1]);
        else
                                        printf("0x%x", allowedshuf[instr->operands[1] - NUM_REGS]);
    } else if(instr->opcode < PSHUFLW)  printf(" m%d, %d", instr->operands[0], instr->operands[2]);
    else                                printf(" m%d, m%d, 0x%x", instr->operands[0], instr->operands[1], instr->operands[2] );
    if (debug && instr->flags)          printf(" *");
}

void print_instructions( program_t *program, int debug )
{
    for( int i = 0; i < program->length[LEN_EFFECTIVE]; i++ ) {
        instruction_t *instr = &program->effective[i];
        print_instruction(instr, debug);
        printf("\n");
    }
}

void print_program( program_t *program, int debug )
{
    printf("length (absolute effective) = %d %d\n", program->length[LEN_ABSOLUTE], program->length[LEN_EFFECTIVE]);
    printf("fitness = %d\n", program->fitness);
    printf("cost = %d\n", program->cost);
    print_instructions(program, debug);
    printf("\n");
}

void execute_instruction( instruction_t instr, register_t *registers )
{
    register_t temp;
    register_t *output = &registers[instr.operands[0]];
    register_t *input1 = &registers[instr.operands[1]];
    uint8_t imm = instr.operands[2];
    int i;

    switch( instr.opcode )
    {
        case PUNPCKLWD:
            temp.wd[0] = output->wd[0];
            temp.wd[2] = output->wd[1];
            temp.wd[4] = output->wd[2];
            temp.wd[6] = output->wd[3];
            temp.wd[1] = input1->wd[0];
            temp.wd[3] = input1->wd[1];
            temp.wd[5] = input1->wd[2];
            temp.wd[7] = input1->wd[3];
            break;
        case PUNPCKHWD:
            temp.wd[0] = output->wd[4];
            temp.wd[2] = output->wd[5];
            temp.wd[4] = output->wd[6];
            temp.wd[6] = output->wd[7];
            temp.wd[1] = input1->wd[4];
            temp.wd[3] = input1->wd[5];
            temp.wd[5] = input1->wd[6];
            temp.wd[7] = input1->wd[7];
            break;
        case PUNCPKLDQ:
            temp.d[0] = output->d[0];
            temp.d[2] = output->d[1];
            temp.d[1] = input1->d[0];
            temp.d[3] = input1->d[1];
            break;
        case PUNPCKHDQ:
            temp.d[0] = output->d[2];
            temp.d[2] = output->d[3];
            temp.d[1] = input1->d[2];
            temp.d[3] = input1->d[3];
            break;
        case PUNPCKLQDQ:
            temp.q[0] = output->q[0];
            temp.q[1] = input1->q[0];
            break;
        case PUNPCKHQDQ:
            temp.q[0] = output->q[1];
            temp.q[1] = input1->q[1];
            break;
        case MOVDQA:
            temp.q[0] = input1->q[0];
            temp.q[1] = input1->q[0];
            break;
        case PSLLDQ:
            if (imm > 16) imm = 16;
            for( i = 0; i < 16 - imm; i++ )
                temp.b[i] = output->b[i+imm];
            for( ; i < 16; i++ )
                temp.b[i] = 0;
            break;
        case PSRLDQ:
            if (imm > 16) imm = 16;
            for( i = 0; i < imm; i++ )
                temp.b[i] = 0;
            for( ; i < 16; i++ )
                temp.b[i] = output->b[i-imm];
            break;
        case PSLLQ:
            if (imm > 64) imm = 64;
            for (i = 0; i < 2; i++)
                temp.q[i] = output->q[i] << imm;
            break;
        case PSRLQ:
            if (imm > 64) imm = 64;
            for (i = 0; i < 2; i++)
                temp.q[i] = output->q[i] >> imm;
            break;
        case PSLLD:
            if (imm > 32) imm = 32;
            for (i = 0; i < 4; i++)
                temp.d[i] = output->d[i] << imm;
            break;
        case PSRLD:
            if (imm > 32) imm = 32;
            for (i = 0; i < 4; i++)
                temp.d[i] = output->d[i] >> imm;
            break;
        case PSHUFLW:
            temp.wd[0] = input1->wd[imm&0x3]; imm >>= 2;
            temp.wd[1] = input1->wd[imm&0x3]; imm >>= 2;
            temp.wd[2] = input1->wd[imm&0x3]; imm >>= 2;
            temp.wd[3] = input1->wd[imm&0x3];
            temp.q[1] = input1->q[1];
            break;
        case PSHUFHW:
            temp.wd[4] = input1->wd[4+(imm&0x3)]; imm >>= 2;
            temp.wd[5] = input1->wd[4+(imm&0x3)]; imm >>= 2;
            temp.wd[6] = input1->wd[4+(imm&0x3)]; imm >>= 2;
            temp.wd[7] = input1->wd[4+(imm&0x3)];
            temp.q[1] = input1->q[1];
            break;
        default:
            fprintf( stderr, "Error: unsupported instruction %d %d %d %d!\n", instr.opcode, instr.operands[0], instr.operands[1], instr.operands[2]);
            assert(instr.opcode < NUM_INSTR);
    }

    memcpy( output, &temp, sizeof(*output) );
}

void init_resultregisters(register_t *reference)
{
    int r, i;
    for(r=0;r<8;r++)
        for(i=0;i<8;i++)
            reference[r].wd[i] = levels[i+r*8];
}

void init_srcregisters(register_t *src)
{
    int r;
    for(r = 0; r < 2; r++)
        memcpy(&src[r], &coeffs[r*8], sizeof(src[0]));
    for(; r < NUM_REGS; r++)
        memset(&src[r], 0, sizeof(src[0]));
}

void init_registers(program_t *program, register_t *src)
{
    memcpy( program->registers, src, sizeof(program->registers) );
}

void init_reference(register_t *src, register_t *reference)
{
    init_levels_4x4();
    init_srcregisters(src);
    init_resultregisters(reference);
}

void init_programs(program_t *programs)
{
    for(int i = 0; i < NUM_PROGRAMS; i++) {
        program_t *program = &programs[i];
        program->length[LEN_ABSOLUTE] = (rand() % (96)) + 5;
        for(int j = 0; j < program->length[LEN_ABSOLUTE]; j++) {
            int instr = rand() % NUM_INSTR;
            int output = rand() % NUM_REGS;
            int input1 = NUM_REGS, input2 = 0;
            instruction_t *instruction = &program->instructions[j];

            /* FIXME: This should be completely random, instead of the guided randomnes we have below.
             * This may help generate more valid code, however.
             */
            input1 = rand() % NUM_REGS;
            if( instr < PSLLDQ )
                input2 = rand() % UINT8_MAX;
            else if( instr < PSLLQ )
                input2 = (rand() % 7) + 1;
            else if( instr < PSLLD )
                input2 = rand() % 64;
            else if( instr < PSHUFLW )
                input2 = rand() % 32;
            else {
//                 input2 = allowedshuf[rand() % 24];
                input2 = rand() % UINT8_MAX;
            }

            assert(instr < NUM_INSTR);
            instruction->opcode = instr;
            instruction->operands[0] = output;
            instruction->operands[1] = input1;
            instruction->operands[2] = input2;
        }
    }
}

void effective_program(program_t *prog)
{
    uint8_t reg_eff[NUM_REGS] = {0};
    int i = prog->length[LEN_ABSOLUTE] - 1;
    int j;

    reg_eff[0] = 1;
    reg_eff[1] = 1;
    /* We want our results in r0-r7. For 4x4 DCT, we only need two result registers. */
    while(i >= 0 && (prog->instructions[i].operands[0] != 0 && prog->instructions[i].operands[0] != 1)) {
        prog->instructions[i].flags = 0;
        i--;
    }
    for( ; i >= 0; i--) {
        instruction_t *instr = &prog->instructions[i];

        instr->flags = 0;
        for (j = 0; j < NUM_REGS; j++)
            if (reg_eff[j] && instr->operands[0] == j)
                instr->flags = 1;

        if (!instr->flags)
            continue;
        if (instr->opcode >= PSHUFLW || instr->opcode == MOVDQA)
            reg_eff[instr->operands[0]] = 0;
        if (instr->operands[1] < NUM_REGS &&
            (instr->opcode < PSLLDQ || instr->opcode > PSRLD))
            reg_eff[instr->operands[1]] = 1;
    }

    for(j = i = 0; i < prog->length[LEN_ABSOLUTE]; i++) {
        if (!prog->instructions[i].flags)
            continue;
        prog->instructions[i].flags = 0;
        prog->effective[j++] = prog->instructions[i];
    }
    prog->length[LEN_EFFECTIVE] = j;
}

int run_program( program_t *program, register_t *src, register_t *reference, int debug )
{
    int i,r,j;

    init_registers(program, src);
    if( debug ) {
//         printf("sourceregs: \n");
//         for(r=0; r<8; r++) {
//             for(j=0; j<8; j++)
//                 printf("%d ", srcregisters[r][j]);
//             printf("\n");
//         }
        printf("targetregs: \n");
        for(r=0;r<8;r++) {
            for(j=0;j<8;j++)
                printf("%d ",reference[r].wd[j]);
            printf("\n");
        }
    }
    for( i = 0; i < program->length[LEN_EFFECTIVE]; i++ ) {
/*
        if(debug) {
            printf("regs: \n");
            for(r=0;r<NUM_REGS;r++) {
                for(j=0;j<8;j++)
                    printf("%d ",registers[r][j]);
                printf("\n");
            }
        }
*/
        execute_instruction( program->effective[i], program->registers );
    }
    if(debug) {
        printf("resultregs: \n");
        for(r=0;r<NUM_REGS;r++) {
            for(j=0;j<8;j++)
                printf("%d ", program->registers[r].wd[j]);
            printf("\n");
        }
    }
    return 1;
}

//#define CHECK_LOC if( i >= 2 && i <= 5 ) continue;
#define CHECK_LOC if( 0 ) continue;

void result_fitness( program_t *prog, register_t *reference )
{
    int sumerror = 0;

    for( int r = 0; r < 2; r++ ) {
        int regerror = 0;
        for(int i = 0; i < 8; i++ )
            regerror += prog->registers[r].wd[i] != reference[r].wd[i];
        sumerror += regerror;
    }

    prog->fitness = sumerror*sumerror;
}

void result_cost( program_t *prog )
{
    /* TODO: Use instruction latency/thouroghput */
//     prog->cost = prog->length[LEN_EFFECTIVE];
    prog->cost = INT_MAX;
}

void instruction_delete( uint8_t (*instructions)[4], int loc, int numinstructions )
{
    int i;
    for( i = loc; i < numinstructions-1; i++ )
    {
        instructions[i][0] = instructions[i+1][0];
        instructions[i][1] = instructions[i+1][1];
        instructions[i][2] = instructions[i+1][2];
        instructions[i][3] = instructions[i+1][3];
    }
}

void instruction_shift( uint8_t (*instructions)[4], int loc, int numinstructions )
{
    int i;
    for( i = numinstructions; i > loc; i-- )
    {
        instructions[i][0] = instructions[i-1][0];
        instructions[i][1] = instructions[i-1][1];
        instructions[i][2] = instructions[i-1][2];
        instructions[i][3] = instructions[i-1][3];
    }
}

void mutate_program( program_t *prog, float probabilities[3] )
{
    int p = rand();
    int ins_idx = rand() % prog->length[LEN_ABSOLUTE];
    instruction_t *instr = &prog->instructions[ins_idx];
    if(p < RAND_MAX * probabilities[0])                                 /* Modify an instruction */
        instr->opcode = rand() % NUM_INSTR;
    else if (p < RAND_MAX * (probabilities[0] + probabilities[1])) {    /* Modify a regester */
        if (rand() < RAND_MAX / 2)
            instr->operands[0] = rand() % NUM_REGS;
        else
            instr->operands[1] = rand() % NUM_REGS;
    } else                                                              /* Modify a constant */
        instr->operands[2] = rand() % UINT8_MAX;

    assert(instr->opcode < NUM_INSTR);
    /* Invalidate existing fitness */
    prog->fitness = INT_MAX;
    prog->cost = 0;
}

int instruction_cost( uint8_t (*instructions)[4], int numinstructions )
{
    int i;
    int cost = 0;
    for( i = 0; i < numinstructions; i++ )
    {
        cost++;
        if( instructions[i][0] != PSHUFLW && instructions[i][0] != PSHUFHW && instructions[i][1] != instructions[i][3] ) cost++;
    }
    return cost;
}

void run_tournament( program_t *programs, program_t *winner, int size)
{
    int contestants[NUM_PROGRAMS];
    int shift = 0;

    contestants[0] = NUM_PROGRAMS;
    for(int i = 0; i < size; i++) {
        contestants[i] = rand() % NUM_PROGRAMS;
        for(int j = 0; j < i; j++) {
            while(contestants[i] == contestants[j])
                contestants[i] = rand() % NUM_PROGRAMS;
        }
    }
    do {
        for(int i = 0; i < size-1; i += (2 << shift)) {
            program_t *a = &programs[contestants[i]];
            program_t *b = &programs[contestants[i + (1<<shift)]];
            if (a->fitness < b->fitness)
                contestants[i] = contestants[i+(1<<shift)];
            else if (a->fitness == b->fitness)
                if (a->cost > b->cost)
                    contestants[i] = contestants[i+(1<<shift)];
        }
        shift++;
    } while(size >> shift > 1);
    memcpy(winner, &programs[contestants[0]], sizeof(*winner));
}

void crossover( program_t *parents, int delta_length, int delta_pos )
{
    program_t temp[2];
    /* FIXME: Respect delta_pos, delta_length */
    int point[2];
    int length[2];

    for(int i = 0; i < 2; i++) {
        point[i] = rand() % parents[i].length[LEN_ABSOLUTE];
        length[i] = (rand() % (parents[i].length[LEN_ABSOLUTE] - point[i])) + 1;
    }

    for(int i = 0; i < 2; i++) {
        if (point[i] + length[!i] > MAX_INSTR)
            length[!i] = MAX_INSTR - point[i];
        if (point[i] + length[i] > parents[i].length[LEN_ABSOLUTE])
            length[i] = parents[i].length[LEN_ABSOLUTE] - point[i];
    }

    for(int j = 0; j < 2; j++) {
        for(int i = 0; i < point[j]; i++) {
            temp[j].instructions[i] = parents[j].instructions[i];
        }
        for(int i = 0; i < length[!j]; i++ ) {
            temp[j].instructions[i+point[j]] = parents[!j].instructions[i+point[!j]];
        }
        for(int i = 0; i < parents[j].length[LEN_ABSOLUTE] - point[j] - length[j]; i++) {
            temp[j].instructions[i+point[j]+length[!j]] = parents[j].instructions[i+point[j]+length[j]];
        }
        temp[j].length[LEN_ABSOLUTE] = point[j] + length[!j] + (parents[j].length[LEN_ABSOLUTE] - point[j] - length[j]);
        temp[j].fitness = INT_MAX;
        temp[j].cost = 0;
    }
    memcpy(parents, temp, sizeof(*parents) *2);
}

int main()
{
    program_t programs[NUM_PROGRAMS];
    int ltime;
    int fitness[2];
    int cost[2];
    int idx[2] = { 0 };
    float probabilities[3] = { 0.4, 0.4, 0.2 };
    program_t winners[2];
    register_t src[NUM_REGS];
    register_t reference[NUM_REGS];

    /* get the current calendar time */
    ltime = time(NULL);
    srand(ltime);

    fitness[0] = INT_MAX;
    fitness[1] = 0;
    cost[0] = INT_MAX;
    cost[1] = 0;

    init_reference(src, reference);
    init_programs(programs);


    for(int i = 0; i < NUM_PROGRAMS; i++) {
        program_t *prog = &programs[i];
        effective_program(prog);
        printf("length (absolute effective)= %d %d, ", prog->length[LEN_ABSOLUTE], prog->length[LEN_EFFECTIVE]);
        run_program(prog, src, reference, 0);
        result_fitness(prog, reference);
        result_cost(prog);

        if (prog->fitness < fitness[0]) {
            fitness[0] = prog->fitness;
            idx[0] = i;
        } else if (prog->fitness == fitness[0]) {
            if (prog->cost && prog->cost < cost[0]) {
                cost[0] = prog->cost;
                idx[0] = i;
            }
        }
        if (prog->fitness > fitness[1]) {
            fitness[1] = prog->fitness;
            idx[1] = i;
        } else if (prog->fitness == fitness[1]) {
            if (prog->cost > cost[1]) {
                cost[1] = prog->cost;
                idx[1] = i;
            }
        }

        printf("fitness = %d\n", prog->fitness);
    }

    /* Best program replaces the worst, with a random chance at mutation */
    memcpy(&programs[idx[1]], &programs[idx[0]], sizeof(programs[0]));
    mutate_program(&programs[idx[1]], probabilities);
    effective_program(&programs[idx[1]]);
    run_program(&programs[idx[1]], src, reference, 0);
    result_fitness(&programs[idx[1]], reference);
    result_cost(&programs[idx[1]]);
    printf("fitness = %d\n", programs[idx[1]].fitness);
    while (fitness[0] > 0) {
        run_tournament(programs, &winners[0], 8);
        run_tournament(programs, &winners[1], 8);
        crossover(winners, 5, 50);
        for(int i = 0; i < 2; i++) {
            if (rand() < RAND_MAX * 0.75)
                mutate_program(&winners[i], probabilities);
            effective_program(&winners[i]);
            run_program(&winners[i], src, reference, 0);
            result_fitness(&winners[i], reference);
            result_cost(&winners[i]);
        }
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < NUM_PROGRAMS; i++) {
                if(programs[i].fitness > winners[j].fitness) {
                    memcpy(&programs[i], &winners[j], sizeof(programs[0]));
                    break;
                }
            }
        }
        for(int i = 0; i < NUM_PROGRAMS; i++) {
            if (fitness[0] > programs[i].fitness) {
                fitness[0] = programs[i].fitness;
                effective_program(&programs[i]);
                run_program(&programs[i], src, reference, 1);
                print_program(&programs[i], 0);
                printf("\n");
            }
        }
    }

    return 0;
}
