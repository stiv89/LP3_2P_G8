#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>

#define MAX_AUTOS 100
#define CAPACIDAD_PUENTE 3
#define LIMITE_SECCION 4
#define ROAD_LENGTH 70

int sigue = 0; // Para el menú
int sentidoimpresion = -1; // -1: ninguno, 0: izquierda, 1: derecha
int interruptor = 1; // Con esto sabemos si se alternó el sentido para que cambie una vez cumplidos 4 autos

// Estructura del auto
typedef struct {
    char id[7];
    int sentido; // 0: izquierda, 1: derecha
    int posicion; // 0: inicio, 1: medio, 2: fin
} Auto;

typedef struct Node {
    Auto data;
    struct Node* next;
} Node;

typedef struct {
    Node* front;
    Node* rear;
    int size;
} Queue;

void initQueue(Queue* q) {
    q->front = q->rear = NULL;
    q->size = 0;
}

int isEmpty(Queue* q) {
    return q->size == 0;
}

void enqueue(Queue* q, Auto data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        fprintf(stderr, "Failed to allocate memory for new node\n");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;
    if (q->rear == NULL) {
        q->front = q->rear = newNode;
    } else {
        q->rear->next = newNode;
        q->rear = newNode;
    }
    q->size++;
}

Auto dequeue(Queue* q) {
    if (isEmpty(q)) {
        fprintf(stderr, "Queue is empty\n");
        exit(EXIT_FAILURE);
    }
    Node* temp = q->front;
    Auto data = temp->data;
    q->front = q->front->next;
    if (q->front == NULL) {
        q->rear = NULL;
    }
    free(temp);
    q->size--;
    return data;
}

Auto front(Queue* q) {
    if (isEmpty(q)) {
        fprintf(stderr, "Queue is empty\n");
        exit(EXIT_FAILURE);
    }
    return q->front->data;
}

int en_puente = 0;       // Número de autos en el puente
int sentido_actual = -1; // -1: ninguno, 0: izquierda, 1: derecha
int contador_autos = 1;  // Contador de autos para el NN

// Declaración de mutexes y una variable de condición para la sincronización de hilos.
pthread_mutex_t bridge_mutex;
pthread_mutex_t queue_mutex;
pthread_cond_t bridge_cond;

int izqcont = 0; // Asegurar que ya cruzaron todos los de izquierda para pasar a derecha
int dercont = 0; // Asegurar que ya cruzaron todos los de derecha para pasar a izquierda

int simulacionCorriendo = 0;// Indica si la simulacion empezo
int autos_pasados_izq = 0;//Cantidad de autos que pasaron por la izq
int autos_pasados_der = 0;//Cantidad de autos que pasaron por la der
int contador_inanicion_izq = 0; // Contador para inanición de autos de la izquierda
int contador_inanicion_der = 0; // Contador para inanición de autos de la derecha

Queue cola_izq; // Cola de autos en el lado izquierdo
Queue cola_der; // Cola de autos en el lado derecho

Auto puente[CAPACIDAD_PUENTE]; // Máximo 3 autos en el puente

// Imprimir los autos en espera
void imprimirEspera() {
    printw("** Autos en espera para atravesar el puente * \n");
    Node* current = cola_izq.front;
    while (current != NULL) {
        printw("<= %s\n", current->data.id);
        current = current->next;
    }
    current = cola_der.front;
    while (current != NULL) {
        printw("=> %s\n", current->data.id);
        current = current->next;
    }
}

// Imprimir el estado del puente
void imprimir_status() {
    clear(); // Limpiar la pantalla antes de imprimir el estado
    printw("Puente: %d autos, Sentido: %s\n", en_puente,
           sentidoimpresion == 0 ? "Izquierda" : (sentidoimpresion == 1 ? "Derecha" : "Ninguno"));
    printw("==================================================================\n");

    // Inicializar una línea para la carretera
    char carretera[ROAD_LENGTH + 1] = "==================================================================";
    carretera[ROAD_LENGTH] = '\0'; // Asegurarse de que la cadena termine correctamente

    // Calcular los índices base para cada sección
    int section_size = ROAD_LENGTH / 3;// Dividimos el camino en tres secciones
    int start_indices[3] = {0, section_size, section_size * 2};// Los indices para cada posicion dentro de la impresion

    // Colocar los autos en la carretera
    for (int i = 0; i < en_puente; i++) {
        int pos = puente[i].posicion;
        int index;

        if (sentidoimpresion == 1) { // Derecha
            index = start_indices[pos] + 5; // Ajustar índice dentro de la sección
            carretera[index] = '>';
            carretera[index + 1] = '>';
            strncpy(&carretera[index + 2], puente[i].id, 6); // Usar hasta tres caracteres del id del auto
        } else if (sentidoimpresion == 0) { // Izquierda
            index = start_indices[2 - pos] + 5; // Ajustar índice dentro de la sección
            carretera[index] = '<';
            carretera[index + 1] = '<';
            strncpy(&carretera[index + 2], puente[i].id, 6); // Usar hasta tres caracteres del id del auto
        }
    }

    // Imprimir la carretera
    printw("%s\n", carretera);
    printw("==================================================================\n");

    if (!simulacionCorriendo) {
        printw("s: Empezar q: Salir b: Atras\n");
        imprimirEspera();
    } else {
        printw("q: Salir\n");
        imprimirEspera();
    }

    refresh();
}

// Mover autos a través del puente
void mover_autos() {

    //Recorremos el array del puente
    for (int i = 0; i < en_puente; i++) {

        if (puente[i].sentido == 0) { // Izquierda
            puente[i].posicion++;
            if (puente[i].posicion == 3) { // Auto ha cruzado el puente
                for (int j = i; j < en_puente - 1; j++) {
                    puente[j] = puente[j + 1];
                }
                en_puente--;
                i--; // Ajustar índice después de eliminar el auto
                izqcont--;
            }
            
        } else if (puente[i].sentido == 1) { // Derecha
            puente[i].posicion++;
            if (puente[i].posicion == 3) { // Auto ha cruzado el puente
                for (int j = i; j < en_puente - 1; j++) {
                    puente[j] = puente[j + 1];
                }
                dercont--;
                en_puente--;
                i--; // Ajustar índice después de eliminar el auto
            }   
        }
    }

    //Si no hay autos en el puente restablecer sentido impresion
    if (en_puente == 0) {
        sentidoimpresion = -1;
    }

}

// Agregar un nuevo auto a la cola correspondiente dependiendo del sentido (0: izquierda, 1: derecha)
void agregar_auto(int sentido) {
    Auto nuevo_auto;
    sprintf(nuevo_auto.id, "auto%02d", contador_autos++); // Formato autoNN
    nuevo_auto.sentido = sentido;
    nuevo_auto.posicion = 0;

    pthread_mutex_lock(&queue_mutex);

    if (sentido == 0) {
        enqueue(&cola_izq, nuevo_auto);
    } else {
        enqueue(&cola_der, nuevo_auto);
    }

    pthread_mutex_unlock(&queue_mutex);
}

// Actualizar el estado del puente y determinar el sentido del tráfico basándose en la cola de espera y las reglas de inanición
void actualizar_puente() {
    pthread_mutex_lock(&queue_mutex);

    // Asegurarse de que el puente esté vacío antes de verificar y cambiar de dirección
    if (en_puente == 0) {
        contador_inanicion_izq = 0;
        contador_inanicion_der = 0;

        if (sentido_actual == 0 && (autos_pasados_izq >= LIMITE_SECCION || isEmpty(&cola_izq)) && !isEmpty(&cola_der)) {
            sentido_actual = 1;
            autos_pasados_izq = 0;
        } else if (sentido_actual == 1 && (autos_pasados_der >= LIMITE_SECCION || isEmpty(&cola_der)) && !isEmpty(&cola_izq)) {
            sentido_actual = 0;
            autos_pasados_der = 0;
        } else if (isEmpty(&cola_izq) && isEmpty(&cola_der)) {
            sentido_actual = -1; // No hay autos en espera, resetear sentido
        }

        // Si no hay autos en el puente y autos de ambos lados en espera, cambiar el sentido a cualquiera que tenga autos
        if (sentido_actual == -1) {
            if (!isEmpty(&cola_izq) && !isEmpty(&cola_der)) {
                sentido_actual = 1; // Ambos sentidos tienen autos, empezar con 1 (derecha)
            } else if (!isEmpty(&cola_izq)) {
                sentido_actual = 0; // Solo hay autos en espera en el lado izquierdo
            } else if (!isEmpty(&cola_der)) {
                sentido_actual = 1; // Solo hay autos en espera en el lado derecho
            }
        }
    }

    // Control de inanición
    if (sentido_actual == 0 && contador_inanicion_izq >= 4 && !isEmpty(&cola_der)) {
        sentido_actual = 1;
        contador_inanicion_izq = 0;
        interruptor = 1;
    } else if (sentido_actual == 1 && contador_inanicion_der >= 4 && !isEmpty(&cola_izq)) {
        sentido_actual = 0;
        contador_inanicion_der = 0;
        interruptor = 0;
    }

    //Para alternar sentidos luego de evitar la inanicion
    if (interruptor == 1) {
        sentido_actual = 1;
    } else if (interruptor == 0) {
        sentido_actual = 0;
    } else if (!isEmpty(&cola_der) && isEmpty(&cola_izq)) {
        sentido_actual = 1;
    } else if (!isEmpty(&cola_izq) && isEmpty(&cola_der)) {
        sentido_actual = 0;
    }

    //Actualizar el sentido si uno de los lados esta vacio y el otro no
    if (sentidoimpresion == -1) {
        if (!isEmpty(&cola_der) && isEmpty(&cola_izq)) {
            sentido_actual = 1;
        } else if (!isEmpty(&cola_izq) && isEmpty(&cola_der)) {
            sentido_actual = 0;
        }
    }

    if(sentido_actual==0 && isEmpty(&cola_izq) && !isEmpty(&cola_der)){
        sentido_actual=1;
    }else if (sentido_actual== 1 && isEmpty(&cola_der) && !isEmpty(&cola_izq)){
        sentido_actual=0;
    }

    //Agregamos el auto al puente
    if (en_puente < CAPACIDAD_PUENTE) {
        if (sentido_actual == 0 && !isEmpty(&cola_izq) && dercont == 0) {
            Auto nuevo_auto = dequeue(&cola_izq);
            nuevo_auto.posicion = 0;
            puente[en_puente++] = nuevo_auto;
            autos_pasados_izq++;
            contador_inanicion_izq++;
            izqcont++;
            sentidoimpresion = 0;
        } else if (sentido_actual == 1 && !isEmpty(&cola_der) && izqcont == 0) {
            Auto nuevo_auto = dequeue(&cola_der);
            nuevo_auto.posicion = 0;
            puente[en_puente++] = nuevo_auto;
            autos_pasados_der++;
            contador_inanicion_der++;
            dercont++;
            sentidoimpresion = 1;
        }
    }

    pthread_mutex_unlock(&queue_mutex);
}

//Menu inicial
void imprimir_menu() {
    clear();
    printw("\nMenu\n");
    printw("s) Iniciar\nt) Status\nq) Salir\n\n<- Car izq\t-> Car der\n>");
    refresh();
}

// Función para imprimir el menú principal de la simulación (hilo de entrada)
void *input_thread(void *arg) {
    while (1) {
        int ch = getch();
        if (ch == KEY_RIGHT) {
            agregar_auto(1);
        } else if (ch == KEY_LEFT) {
            agregar_auto(0);
        } else if (ch == 's') {
            pthread_mutex_lock(&bridge_mutex);
            // Determinar el sentido inicial basado en los autos en espera
            if (sentido_actual == -1) {
                if (!isEmpty(&cola_izq) && !isEmpty(&cola_der)) {
                    sentido_actual = 1;// Siempre empieza en derecha
                } else if (!isEmpty(&cola_izq)) {
                    sentido_actual = 0; // Solo hay autos en espera en el lado izquierdo
                } else if (!isEmpty(&cola_der)) {
                    sentido_actual = 1; // Solo hay autos en espera en el lado derecho
                }
            }
            simulacionCorriendo = 1;
            pthread_mutex_unlock(&bridge_mutex);
        } else if (ch == 'q') {
            endwin();
            exit(0);
        } else if (ch == 't') {
            sigue = 1;
        } else if (ch == 'b') {
            sigue = 0;
        }
    }
    return NULL;
}

int main() {
    pthread_mutex_init(&bridge_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&bridge_cond, NULL);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    initQueue(&cola_izq);
    initQueue(&cola_der);
    //Inicializamos el array
    for (int i = 0; i < CAPACIDAD_PUENTE; i++) {
        puente[i].id[0] = '\0';
    }

    pthread_t tid;
    pthread_create(&tid, NULL, input_thread, NULL);

    sentido_actual = -1;

    while (1) {
        if (simulacionCorriendo) {
            pthread_mutex_lock(&bridge_mutex);
            mover_autos();
            actualizar_puente();
            imprimir_status();
            pthread_mutex_unlock(&bridge_mutex);
            usleep(1000000); // Pausa de 1 segundo para actualizar el estado
        } else {
            if (sigue != 1) {
                imprimir_menu();
                usleep(100000); // Pausa de 0.1 segundo para reducir uso de CPU mientras espera
            } else {
                pthread_mutex_lock(&bridge_mutex);
                imprimir_status();
                pthread_mutex_unlock(&bridge_mutex);
                usleep(100000); // Pausa de 0.1 segundo para reducir uso de CPU mientras espera
            }
        }
    }

    endwin();

    pthread_mutex_destroy(&bridge_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&bridge_cond);

    return 0;
}
