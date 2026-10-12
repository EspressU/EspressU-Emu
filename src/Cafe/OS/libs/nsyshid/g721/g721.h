
#ifndef _G72X_H
#define _G72X_H

struct g72x_state {
    long yl;
    short yu;
    short dms;
    short dml;
    short ap;

    short a[2];
    short b[6];
    short pk[2];
    short dq[6];
    short sr[2];
    char td;
};

void g72x_init_state(struct g72x_state*);
int g721_encoder(int sample, struct g72x_state* state_ptr);
int g721_decoder(int code, struct g72x_state* state_ptr);

int quantize(int d, int y, short* table, int size);
int reconstruct(int, int, int);
void

    update(int code_size,
                  int y,
                  int wi,
                  int fi,
                  int dq,
                  int sr,
                  int dqsez,
                  struct g72x_state* state_ptr);

int predictor_zero(struct g72x_state* state_ptr);

int predictor_pole(struct g72x_state* state_ptr);
int step_size(struct g72x_state* state_ptr);
#endif

