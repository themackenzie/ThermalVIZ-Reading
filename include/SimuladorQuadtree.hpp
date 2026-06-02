#ifndef SIMULADOR_QUADTREE_HPP
#define SIMULADOR_QUADTREE_HPP

#include "Quadtree.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include <cmath>

class SimuladorQuadtree {
private:
    std::shared_ptr<NodoQuadtree> raiz;
    float resolucionMinima; // Tamaño de la celda más pequeña permitida
    float alpha;            // Coeficiente de conductividad térmica




    void borradoProfundoYColapso(std::shared_ptr<NodoQuadtree> nodo, float x, float y, float resMin) {
        if (!nodo) return;

        // Verificar si el punto del mouse está dentro de los límites de este nodo
        if (x < nodo->limites.x - nodo->limites.medioAncho || x > nodo->limites.x + nodo->limites.medioAncho ||
            y < nodo->limites.y - nodo->limites.medioAlto || y > nodo->limites.y + nodo->limites.medioAlto) {
            return; 
        }

        // Si llegamos al nivel de la celda que queremos borrar (o si es una hoja)
        if (!nodo->tieneHijos || (nodo->limites.medioAncho <= resMin)) {
            nodo->esPared = false;
            nodo->esFuente = false;
            nodo->temperatura = 0.0f; // Forzamos a cero absoluto/base para matar el calor de golpe
            return;
        }

        // Si tiene hijos, descendemos recursivamente para limpiar las hojas
        borradoProfundoYColapso(nodo->noroeste, x, y, resMin);
        borradoProfundoYColapso(nodo->noreste,  x, y, resMin);
        borradoProfundoYColapso(nodo->suroeste, x, y, resMin);
        borradoProfundoYColapso(nodo->sureste,  x, y, resMin);

        // --- EL TRUCO MAESTRO ---
        // Después de limpiar los hijos, verificamos si todos quedaron libres de paredes/fuentes
        // y con temperaturas idénticas (0.0f). Si es así, los colapsamos AQUÍ MISMO de forma dinámica.
        if (!nodo->noroeste->tieneHijos && !nodo->noreste->tieneHijos &&
            !nodo->suroeste->tieneHijos && !nodo->sureste->tieneHijos) {
            
            if (!nodo->noroeste->esPared && !nodo->noreste->esPared && 
                !nodo->suroeste->esPared && !nodo->sureste->esPared &&
                nodo->noroeste->temperatura == 0.0f && nodo->noreste->temperatura == 0.0f &&
                nodo->suroeste->temperatura == 0.0f && nodo->sureste->temperatura == 0.0f) {
                
                nodo->temperatura = 0.0f;
                nodo->tieneHijos = false;
                // Liberamos la memoria de los 4 hijos en tiempo real
                nodo->noroeste = nodo->noreste = nodo->suroeste = nodo->sureste = nullptr;
            }
        }
    }



    // Función auxiliar recursiva para calcular la transferencia de calor
    void calcularDifusion(std::shared_ptr<NodoQuadtree> nodo, std::vector<std::vector<float>>& matrizVieja, int resAncho, int resAlto) {
        if (!nodo) return;

        // CRÍTICO: Si el nodo mismo está marcado como pared, forzamos su estado estructural 
        // e impedimos que la física de fluidos altere su valor base.
        if (nodo->esPared || nodo->temperatura < -0.5f) {
            nodo->temperatura = -1.0f;
            return;
        }

        // Si es una hoja, no es una fuente fija de calor Y tampoco es una pared... calculamos física
        if (!nodo->tieneHijos && !nodo->esFuente) {
            int j = static_cast<int>((nodo->limites.x + 1.0f) * 0.5f * resAncho);
            int i = static_cast<int>((1.0f - nodo->limites.y) * 0.5f * resAlto);

            if (i > 0 && i < resAlto - 1 && j > 0 && j < resAncho - 1) {
                float tActual = matrizVieja[i][j];
                
                // Vecinos térmicos
                float tArriba    = matrizVieja[i-1][j];
                float tAbajo     = matrizVieja[i+1][j];
                float tIzquierda = matrizVieja[i][j-1];
                float tDerecha   = matrizVieja[i][j+1];

                // --- LÓGICA DE BARRERA ADIABÁTICA BLINDADA ---
                // Si el vecino es un muro gris (< -0.5f), rebota el flujo térmico anulando el gradiente
                if (tArriba    < -0.5f) tArriba    = tActual; 
                if (tAbajo     < -0.5f) tAbajo     = tActual;
                if (tIzquierda < -0.5f) tIzquierda = tActual;
                if (tDerecha   < -0.5f) tDerecha   = tActual;

                float flujoCalor = (tArriba + tAbajo + tIzquierda + tDerecha - 4.0f * tActual);
                nodo->temperatura += alpha * flujoCalor;
            }
        }

        if (nodo->tieneHijos) {
            calcularDifusion(nodo->noroeste, matrizVieja, resAncho, resAlto);
            calcularDifusion(nodo->noreste, matrizVieja, resAncho, resAlto);
            calcularDifusion(nodo->suroeste, matrizVieja, resAncho, resAlto);
            calcularDifusion(nodo->sureste, matrizVieja, resAncho, resAlto);

            // El padre promedia el calor de los hijos, cuidando de no absorber los marcadores de pared
            float sumaValores = 0.0f;
            int hijosValidos = 0;
            
            auto procesarHijo = [&](const std::shared_ptr<NodoQuadtree>& h) {
                if (h && !h->esPared && h->temperatura >= 0.0f) {
                    sumaValores += h->temperatura;
                    hijosValidos++;
                }
            };
            procesarHijo(nodo->noroeste); procesarHijo(nodo->noreste);
            procesarHijo(nodo->suroeste); procesarHijo(nodo->sureste);

            if (hijosValidos > 0) {
                nodo->temperatura = sumaValores / hijosValidos;
            }
        }
    }

    // Comprimir nodos hijos cuyas temperaturas sean casi idénticas (Optimización de RAM)
    void optimizarArbol(std::shared_ptr<NodoQuadtree> nodo) {
        if (!nodo || !nodo->tieneHijos) return;

        optimizarArbol(nodo->noroeste);
        optimizarArbol(nodo->noreste);
        optimizarArbol(nodo->suroeste);
        optimizarArbol(nodo->sureste);

        // MODIFICACIÓN DE SEGURIDAD: Si alguno de los hijos es un muro, ABORTAMOS la optimización de esta rama.
        // Esto evita que el árbol destruya o colapse la geometría de tus muros grises.
        if (nodo->noroeste->esPared || nodo->noreste->esPared || 
            nodo->suroeste->esPared || nodo->sureste->esPared) {
            return; 
        }

        if (!nodo->noroeste->tieneHijos && !nodo->noreste->tieneHijos &&
            !nodo->suroeste->tieneHijos && !nodo->sureste->tieneHijos) {
            
            float t1 = nodo->noroeste->temperatura;
            float t2 = nodo->noreste->temperatura;
            float t3 = nodo->suroeste->temperatura;
            float t4 = nodo->sureste->temperatura;

            float d1 = std::abs(t1 - t2);
            float d2 = std::abs(t1 - t3);
            float d3 = std::abs(t1 - t4);

            float maxDiff = std::max(d1, std::max(d2, d3));

            if (maxDiff < 0.5f && !nodo->noroeste->esFuente && !nodo->noreste->esFuente && 
                !nodo->suroeste->esFuente && !nodo->sureste->esFuente) {
                
                nodo->temperatura = (t1 + t2 + t3 + t4) * 0.25f;
                nodo->tieneHijos = false;
                nodo->noroeste = nodo->noreste = nodo->suroeste = nodo->sureste = nullptr;
            }
        }
    }

public:
    SimuladorQuadtree(float cond = 0.05f) : alpha(cond) {
        Rect limitesRaiz(0.0f, 0.0f, 1.0f, 1.0f);
        raiz = std::make_shared<NodoQuadtree>(limitesRaiz, 20.0f);
        resolucionMinima = 1.0f / 128.0f; 
    }

    // MÉTODOS PÚBLICOS VISIBLES DESDE MAIN.CPP
    void aplicarCalor(float x, float y, float temp) {
        if (!raiz) return;
        // El calor normal sigue usando tu lógica nativa de inyección térmica
        raiz->insertarPuntoCalor(x, y, temp, resolucionMinima);
    }

    // NUEVO: Método directo para el MODO 2 (Construir Muros) sin tocar la física térmica
    void tallarMuroEstructural(float x, float y) {
        if (!raiz) return;
        raiz->alterarEstadoPared(x, y, true, resolucionMinima);
    }

    void marcarNodoComoPared(std::shared_ptr<NodoQuadtree> nodo, float x, float y, bool desactivar) {
        if (!nodo) return;
        if (x < nodo->limites.x - nodo->limites.medioAncho || x > nodo->limites.x + nodo->limites.medioAncho ||
            y < nodo->limites.y - nodo->limites.medioAlto || y > nodo->limites.y + nodo->limites.medioAlto) return;

        if (!nodo->tieneHijos) {
            nodo->esPared = !desactivar;
            nodo->temperatura = desactivar ? 20.0f : -1.0f;
            return;
        }
        marcarNodoComoPared(nodo->noroeste, x, y, desactivar);
        marcarNodoComoPared(nodo->noreste, x, y, desactivar);
        marcarNodoComoPared(nodo->suroeste, x, y, desactivar);
        marcarNodoComoPared(nodo->sureste, x, y, desactivar);
    }

    // NUEVO: Método directo para el MODO 1 (Borrador) - Apaga la booleana y resetea a 20°C
    void limpiarCeldaCompleta(float x, float y) {
        if (!raiz) return;
        borradoProfundoYColapso(raiz, x, y, resolucionMinima);
    }
    void actualizar() {
        if (!raiz) return;

        int resAncho = 256; 
        int resAlto = 256;  
        
        std::vector<std::vector<float>> matrizLectura(resAlto, std::vector<float>(resAncho, 0.0f));
        
        // Ahora aplanarAMatriz escribirá los -1.0f de los muros directamente aquí
        raiz->aplanarAMatriz(matrizLectura, resAncho, resAlto);

        calcularDifusion(raiz, matrizLectura, resAncho, resAlto);
        optimizarArbol(raiz);
    }

    std::vector<float> obtenerMatrizPlana(int resAncho, int resAlto) {
        std::vector<std::vector<float>> matriz(resAlto, std::vector<float>(resAncho, 0.0f));
        raiz->aplanarAMatriz(matriz, resAncho, resAlto);

        std::vector<float> datosPlano(resAncho * resAlto * 3, 0.0f);

        for (int i = 0; i < resAlto; ++i) {
            for (int j = 0; j < resAncho; ++j) {
                int indiceBuffer = (i * resAncho + j) * 3;

                // Si es muro estructural, se queda intacto con su canal Verde en 1.0
                if (matriz[i][j] > 900.0f) {
                    datosPlano[indiceBuffer + 0] = 0.0f; 
                    datosPlano[indiceBuffer + 1] = 1.0f; 
                    datosPlano[indiceBuffer + 2] = 0.0f; 
                    continue;
                }

                if (i > 0 && i < resAlto - 1 && j > 0 && j < resAncho - 1) {
                    float tActual = matriz[i][j];
                    
                    float vArriba    = matriz[i-1][j];
                    float vAbajo     = matriz[i+1][j];
                    float vIzquierda = matriz[i][j-1];
                    float vDerecha   = matriz[i][j+1];

                    // =========================================================
                    // DINÁMICA DE REBOTE Y CHOQUE TÉRMICO (GRADIENTE FRONTERA)
                    // =========================================================
                    // Si el vecino es un muro, hacemos que absorba/refleje frío (0.0).
                    // Esto fuerza a que la matemática genere un degradado natural de caída
                    // justo antes de tocar el bloque gris, amoldando el contorno del fuego.
                    if (vArriba    > 900.0f) vArriba    = tActual * 0.4f;
                    if (vAbajo     > 900.0f) vAbajo     = tActual * 0.4f;
                    if (vIzquierda > 900.0f) vIzquierda = tActual * 0.4f;
                    if (vDerecha   > 900.0f) vDerecha   = tActual * 0.4f;

                    float suma = tActual * 0.36f; 
                    suma += (vArriba + vAbajo + vIzquierda + vDerecha) * 0.16f; 
                    
                    datosPlano[indiceBuffer + 0] = suma / 400.0f; // Canal R (Calor)
                    datosPlano[indiceBuffer + 1] = 0.0f;          // Canal G
                    datosPlano[indiceBuffer + 2] = 0.0f;          
                } else {
                    datosPlano[indiceBuffer + 0] = matriz[i][j] / 400.0f;
                    datosPlano[indiceBuffer + 1] = 0.0f;
                    datosPlano[indiceBuffer + 2] = 0.0f;
                }
            }
        }
        return datosPlano;
    }

    void recolectarBordesNodos(std::shared_ptr<NodoQuadtree> nodo, std::vector<float>& cajas) {
        if (!nodo) return;

        // Mandamos los 4 límites espaciales tradicionales
        cajas.push_back(nodo->limites.x - nodo->limites.medioAncho);
        cajas.push_back(nodo->limites.y - nodo->limites.medioAlto);
        cajas.push_back(nodo->limites.x + nodo->limites.medioAncho);
        cajas.push_back(nodo->limites.y + nodo->limites.medioAlto);
        
        // ¡NUEVO!: Añadimos la temperatura del nodo como quinta componente
        // Si es una pared estructural, le pasamos un flag numérico (-1.0f) para que el visor lo sepa
        if (nodo->esPared) {
            cajas.push_back(-1.0f);
        } else {
            cajas.push_back(nodo->temperatura);
        }

        if (nodo->tieneHijos) {
            recolectarBordesNodos(nodo->noroeste, cajas);
            recolectarBordesNodos(nodo->noreste,  cajas);
            recolectarBordesNodos(nodo->suroeste, cajas);
            recolectarBordesNodos(nodo->sureste,  cajas);
        }
    }

    // Método público que invoca el visor desde main.cpp
    std::vector<float> obtenerLimitesNodos() {
        std::vector<float> cajas;
        if (raiz) {
            recolectarBordesNodos(raiz, cajas);
        }
        return cajas;
    }
};

#endif