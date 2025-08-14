// Librerias
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifdef _OPENMP
  #include <omp.h>
#endif

/* COLORES */
#define ROJO     "\x1b[31m"
#define VERDE    "\x1b[32m"
#define AMARILLO "\x1b[33m"
#define RESET    "\x1b[0m"

/* -------------------- Estructuras del modelo -------------------- */
typedef struct {
    int id;
    char origen;  
    char giro;    
    float tArribo;
} Vehiculo;

typedef struct {
    int id;
    char estado;     
    float tRestante; 
} Semaforo;

typedef struct {
    int id;
    int colas[8];         
    Semaforo semaforos[4];
    int faseActual;        
    float reloj;          
} Interseccion;


/* -------------------- Colas circulares (ring buffers) -------------------- */
#define MAXQ 4096
static Vehiculo Q[8][MAXQ];
static int HEAD[8], TAIL[8];

// static inline int giro_offset(char g){ return (g=='R'?0:(g=='I'?1:2)); }
// static inline int dir_base(char d){ return (d=='N'?0:(d=='S'?1:(d=='E'?2:3))); }
// static inline int idx_mov(char d, char g){ return dir_base(d)*3 + giro_offset(g); }
/* Mapeos de (dirección, giro) → índice 0..7 */
static inline int giro_offset(char g){ return (g=='R'?0:1); }
static inline int dir_base(char d){ return (d=='N'?0:(d=='S'?1:(d=='E'?2:3))); }
static inline int idx_mov(char d, char g){ return dir_base(d)*2 + giro_offset(g); }

/* Inserta un vehículo en la cola 'idx'. 1=ok, 0=llena */
static int push_vehiculo(int idx, Vehiculo v){
    int next = (TAIL[idx]+1) % MAXQ;
    if(next == HEAD[idx]) return 0; 
    Q[idx][TAIL[idx]] = v;
    TAIL[idx] = next;
    return 1;
}

/* Extrae un vehículo de la cola 'idx'. 1=ok, 0=vacía */
static int pop_vehiculo(int idx, Vehiculo* out){
    if(HEAD[idx]==TAIL[idx]) return 0; 
    *out = Q[idx][HEAD[idx]];
    HEAD[idx] = (HEAD[idx]+1) % MAXQ;
    return 1;
}

/* INDICE DE CADA DIRECCION EN EL ARRAY */
static inline int dir_index(char d){
    switch(d){ case 'N': return 0; case 'S': return 1; case 'E': return 2; default: return 3; }
}
static inline double frand() { return rand() / (double)RAND_MAX; }

/* Generador de llegadas */
void generarLlegadas(Interseccion* I, const double dir_lambda[4], int* next_id){
    // const double pR=0.6, pI=0.2, pD=0.2;
    const double pR=0.6, pD=0.4;
    const char DIRS[4] = {'N','S','E','O'};
    // const char GIROS[3] = {'R','I','D'};
    const char GIROS[2] = {'R','D'};

    for(int d=0; d<4; ++d){
        if(frand() < dir_lambda[d]){
            double r = frand();
            // char g = (r < pR ? GIROS[0] : (r < pR+pI ? GIROS[1] : GIROS[2]));
            char g = (r < pR ? GIROS[0] : GIROS[1]);
            Vehiculo v = { .id = (*next_id)++, .origen = DIRS[d], .giro = g, .tArribo = I->reloj };
            int idx = idx_mov(v.origen, v.giro);
            if(push_vehiculo(idx, v)){
                I->colas[idx]++;
                #pragma omp critical
                printf(VERDE "*" RESET " Llega Vehiculo #%d (%c-%c) -> cola[%d] = %d\n",
                       v.id, v.origen, v.giro, idx, I->colas[idx]);
            }else{
                #pragma omp critical
                printf(ROJO "!" RESET " Cola llena! Perdido Vehiculo #%d (%c-%c)\n",
                       v.id, v.origen, v.giro);
            }
        }
    }
}

/* -------------------- Servicio por dirección -------------------- */
void servir_direccion(Interseccion* I, char dir, int mu){
    int di = dir_index(dir);
    if(I->semaforos[di].estado != 'V'){
        #pragma omp critical
        printf(ROJO"->" RESET " %c en %c: no se atiende (semáforo no VERDE)\n",
               dir, I->semaforos[di].estado);
        return;
    }

    // const char GIROS[3] = {'R','I','D'};
    const char GIROS[3] = {'R','D'};

    #pragma omp parallel for schedule(dynamic,1)
    for(int g=0; g<2; ++g){
        int idx = idx_mov(dir, GIROS[g]);
        // int idx = idx_mov(dir, 'D');
        int atendidos = 0;
        for(int k=0; k<mu; ++k){
            Vehiculo v;
            if(!pop_vehiculo(idx, &v)) break;
            I->colas[idx]--;
            atendidos++;
            double espera = I->reloj - v.tArribo;
            #pragma omp critical
            printf(VERDE"$" RESET " Sale Vehiculo #%d (%c-%c), espera=%.0fs | cola[%d]=%d\n",
                   v.id, v.origen, v.giro, espera, idx, I->colas[idx]);
        }
        if(atendidos==0){
            #pragma omp critical
            printf(AMARILLO"$" RESET " %c-%c sin vehiculos\n", dir, GIROS[g]);
        }
    }
}

/* -------------------- Helpers de fase (estados de semáforos) -------------------- */
void set_verde_NS(Interseccion* I, int tRest){
    I->semaforos[0].estado='V'; I->semaforos[0].tRestante=tRest;
    I->semaforos[1].estado='V'; I->semaforos[1].tRestante=tRest;
    I->semaforos[2].estado='R'; I->semaforos[2].tRestante=0;
    I->semaforos[3].estado='R'; I->semaforos[3].tRestante=0;
}
void set_verde_EO(Interseccion* I, int tRest){
    I->semaforos[2].estado='V'; I->semaforos[2].tRestante=tRest;
    I->semaforos[3].estado='V'; I->semaforos[3].tRestante=tRest;
    I->semaforos[0].estado='R'; I->semaforos[0].tRestante=0;
    I->semaforos[1].estado='R'; I->semaforos[1].tRestante=0;
}
void set_amarillo_fase(Interseccion* I, int tRest){
    if(I->faseActual==0){
        I->semaforos[0].estado='A'; I->semaforos[0].tRestante=tRest;
        I->semaforos[1].estado='A'; I->semaforos[1].tRestante=tRest;
        I->semaforos[2].estado='R'; I->semaforos[2].tRestante=0;
        I->semaforos[3].estado='R'; I->semaforos[3].tRestante=0;
    }else{
        I->semaforos[2].estado='A'; I->semaforos[2].tRestante=tRest;
        I->semaforos[3].estado='A'; I->semaforos[3].tRestante=tRest;
        I->semaforos[0].estado='R'; I->semaforos[0].tRestante=0;
        I->semaforos[1].estado='R'; I->semaforos[1].tRestante=0;
    }
}
void set_all_red(Interseccion* I, int tRest){
    for(int d=0; d<4; ++d){ I->semaforos[d].estado='R'; I->semaforos[d].tRestante=tRest; }
}

/* Reduccion visual de tiempos (informativo) */
void decrementar_tiempos(Semaforo s[4]){
    for(int i=0;i<4;++i){
        if(s[i].tRestante > 0) s[i].tRestante -= 1.0f;
        if(s[i].tRestante < 0) s[i].tRestante = 0;
    }
}

/* Impresión del estado de semáforos en este tick */
void imprimir_estados(const Interseccion* I){
    printf(VERDE "Semaforos t=%.0fs: N=%c(%.0f) S=%c(%.0f) E=%c(%.0f) O=%c(%.0f)\n" RESET,
        I->reloj,
        I->semaforos[0].estado, I->semaforos[0].tRestante,
        I->semaforos[1].estado, I->semaforos[1].tRestante,
        I->semaforos[2].estado, I->semaforos[2].tRestante,
        I->semaforos[3].estado, I->semaforos[3].tRestante
    );
}

/* MAIN :O */
int main(void){
    srand((unsigned)time(NULL));
    memset(HEAD, 0, sizeof(HEAD));
    memset(TAIL, 0, sizeof(TAIL));

    Interseccion I = {0};
    I.id = 1;
    for(int i=0;i<8;++i) I.colas[i]=0;
    for(int i=0;i<4;++i){ I.semaforos[i].id=i; I.semaforos[i].estado='R'; I.semaforos[i].tRestante=0; }
    I.faseActual = 0;
    I.reloj = 0.0f;

    const double lambda[4] = {0.8, 0.6, 0.7, 0.5}; 
    const int mu = 2;          
    const int tVerde = 8;     
    const int tAmarillo = 2;  
    const int tAllRed = 1;    
    const int Tfin = 40;     
    int tFase = 0;
    int next_id = 1;

    while ((int)I.reloj < Tfin){
        generarLlegadas(&I, lambda, &next_id);

        if(tFase < tVerde){
            if(I.faseActual==0){
                if(tFase==0) printf("[t=%3.0f] VERDE NS\n", I.reloj);
                set_verde_NS(&I, tVerde - tFase);

                #pragma omp parallel sections
                {
                    #pragma omp section
                    { servir_direccion(&I, 'N', mu); }
                    #pragma omp section
                    { servir_direccion(&I, 'S', mu); }
                }
            }else{
                if(tFase==0) printf("[t=%3.0f] VERDE EO\n", I.reloj);
                set_verde_EO(&I, tVerde - tFase);

                #pragma omp parallel sections
                {
                    #pragma omp section
                    { servir_direccion(&I, 'E', mu); }
                    #pragma omp section
                    { servir_direccion(&I, 'O', mu); }
                }
            }
        }else if(tFase < tVerde + tAmarillo){
            if(tFase==tVerde) printf("[t=%3.0f] AMARILLO (%s)\n", I.reloj, (I.faseActual==0?"NS":"EO"));
            set_amarillo_fase(&I, tVerde + tAmarillo - tFase);
        }else if(tFase < tVerde + tAmarillo + tAllRed){
            if(tFase==tVerde + tAmarillo) printf("[t=%3.0f] TODO ROJO\n", I.reloj);
            set_all_red(&I, tVerde + tAmarillo + tAllRed - tFase);
        }else{
            I.faseActual = 1 - I.faseActual;
            tFase = 0;
            printf("[t=%3.0f] Cambio de fase -> %s\n", I.reloj, (I.faseActual==0?"NS":"EO"));
            I.reloj += 1.0f;
            continue;
        }

        imprimir_estados(&I);
        decrementar_tiempos(I.semaforos);

        tFase += 1;
        I.reloj += 1.0f;
    }
    return 0;
}
