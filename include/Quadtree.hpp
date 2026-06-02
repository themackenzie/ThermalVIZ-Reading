#ifndef QUADTREE_HPP
#define QUADTREE_HPP

#include <vector>
#include <memory>

// Estructura que define los límites geográficos de cada nodo
struct Rect {
    float x;      // Centro X (o esquina, según tu implementación)
    float y;      // Centro Y
    float medioAncho;  // Medio ancho
    float medioAlto;   // Medio alto

    // ¡AGREGAR ESTE CONSTRUCTOR EXPÍCITO!
    Rect(float _x = 0.0f, float _y = 0.0f, float _medioAncho = 0.0f, float _medioAlto = 0.0f)
        : x(_x), y(_y), medioAncho(_medioAncho), medioAlto(_medioAlto) {}
};

class NodoQuadtree {
public:
    Rect limites;
    float temperatura;
    bool esFuente;
    bool tieneHijos;

    bool esPared;

    // Los 4 cuadrantes hijos
    std::shared_ptr<NodoQuadtree> noroeste;
    std::shared_ptr<NodoQuadtree> noreste;
    std::shared_ptr<NodoQuadtree> suroeste;
    std::shared_ptr<NodoQuadtree> sureste;

    // Constructor del nodo
    NodoQuadtree(Rect _limites, float _temperatura = 20.0f)
        : limites(_limites), temperatura(_temperatura), tieneHijos(false), esFuente(false), esPared(false) {}

    // Subdividir el nodo actual en 4 cuadrantes más pequeños
    void subdividir() {
        float hAncho = limites.medioAncho * 0.5f;
        float hAlto = limites.medioAlto * 0.5f;

        noroeste = std::make_shared<NodoQuadtree>(Rect{limites.x - hAncho, limites.y + hAlto, hAncho, hAlto}, temperatura);
        noreste  = std::make_shared<NodoQuadtree>(Rect{limites.x + hAncho, limites.y + hAlto, hAncho, hAlto}, temperatura);
        suroeste = std::make_shared<NodoQuadtree>(Rect{limites.x - hAncho, limites.y - hAlto, hAncho, hAlto}, temperatura);
        sureste  = std::make_shared<NodoQuadtree>(Rect{limites.x + hAncho, limites.y - hAlto, hAncho, hAlto}, temperatura);

        tieneHijos = true;
    }

    void alterarEstadoPared(float px, float py, bool activarPared, float resMinima) {
        // Si el punto está fuera de los límites de este nodo, abortamos inmediatamente
        if (px < limites.x - limites.medioAncho || px > limites.x + limites.medioAncho ||
            py < limites.y - limites.medioAlto || py > limites.y + limites.medioAlto) {
            return;
        }

        // Si alcanzamos la resolución mínima, esta hoja es nuestro píxel objetivo
        if (limites.medioAncho <= resMinima) {
            esPared = activarPared;
            if (activarPared) {
                temperatura = -1.0f; // Flag numérico para la matriz y el shader
                esFuente = false;    // Un muro no puede ser fuente de calor
            } else {
                temperatura = 20.0f; // El borrador lo regresa a la temperatura normal
            }
            return;
        }

        // Si no tiene hijos y necesitamos seguir bajando, subdividimos
        if (!tieneHijos) {
            subdividir();
        }

        // Propagamos a los hijos de forma directa
        noroeste->alterarEstadoPared(px, py, activarPared, resMinima);
        noreste->alterarEstadoPared(px, py, activarPared, resMinima);
        suroeste->alterarEstadoPared(px, py, activarPared, resMinima);
        sureste->alterarEstadoPared(px, py, activarPared, resMinima);

        // Al regresar, si es una rama con hijos, actualizamos la temperatura del padre
        if (tieneHijos) {
            temperatura = (noroeste->temperatura + noreste->temperatura + 
                           suroeste->temperatura + sureste->temperatura) * 0.25f;
        }
    }

    // Insertar un punto de calor de forma jerárquica
    void insertarPuntoCalor(float px, float py, float temp, float resolucionMinima) {
        // Verificar si el punto cae dentro de los límites de este nodo
        if (px < limites.x - limites.medioAncho || px > limites.x + limites.medioAncho ||
            py < limites.y - limites.medioAlto || py > limites.y + limites.medioAlto) {
            return; 
        }

        // Si ya alcanzamos el tamaño mínimo de celda, nos detenemos y aplicamos el calor aquí
        if (limites.medioAncho <= resolucionMinima) {
            temperatura = temp;
            esFuente = true;
            return;
        }

        // Si no tiene hijos, lo subdividimos para poder bajar en el árbol
        if (!tieneHijos) {
            subdividir();
        }

        // Delegar la inserción al hijo correspondiente
        noroeste->insertarPuntoCalor(px, py, temp, resolucionMinima);
        noreste->insertarPuntoCalor(px, py, temp, resolucionMinima);
        suroeste->insertarPuntoCalor(px, py, temp, resolucionMinima);
        sureste->insertarPuntoCalor(px, py, temp, resolucionMinima);
        
        // Al ser una fuente interna, actualizamos el promedio de temperatura del padre
        temperatura = (noroeste->temperatura + noreste->temperatura + suroeste->temperatura + sureste->temperatura) * 0.25f;
    }
        
    void aplanarAMatriz(std::vector<std::vector<float>>& matriz, int resAncho, int resAlto) const {
        if (!tieneHijos) {
            int xMin = static_cast<int>(((limites.x - limites.medioAncho) + 1.0f) * 0.5f * resAncho);
            int xMax = static_cast<int>(((limites.x + limites.medioAncho) + 1.0f) * 0.5f * resAncho);
            int yMin = static_cast<int>((1.0f - (limites.y + limites.medioAlto)) * 0.5f * resAlto);
            int yMax = static_cast<int>((1.0f - (limites.y - limites.medioAlto)) * 0.5f * resAlto);

            xMin = std::max(0, std::min(xMin, resAncho - 1));
            xMax = std::max(0, std::min(xMax, resAncho));
            yMin = std::max(0, std::min(yMin, resAlto - 1));
            yMax = std::max(0, std::min(yMax, resAlto));

            for (int i = yMin; i < yMax; ++i) {
                for (int j = xMin; j < xMax; ++j) {
                    if (esPared) {
                        matriz[i][j] = 999.0f; // Flag seguro para OpenGL (Muro gris)
                    } else {
                        matriz[i][j] = temperatura; // Temperatura pura (0.0 a 400.0)
                    }
                }
            }
            return;
        }

        if (noroeste) noroeste->aplanarAMatriz(matriz, resAncho, resAlto);
        if (noreste)  noreste->aplanarAMatriz(matriz, resAncho, resAlto);
        if (suroeste) suroeste->aplanarAMatriz(matriz, resAncho, resAlto);
        if (sureste)  sureste->aplanarAMatriz(matriz, resAncho, resAlto);
    }
};

#endif