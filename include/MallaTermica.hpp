#ifndef MALLA_TERMICA_HPP
#define MALLA_TERMICA_HPP

#include <vector>

// Estructura para cada celda de la cuadrícula
struct Celda {
    float temperatura;
    bool esFuente; // Si es verdadero, mantiene su temperatura fija (punto de calor)
};

class MallaTermica {
private:
    int filas;
    int columnas;
    std::vector<std::vector<Celda>> mallaActual;
    std::vector<std::vector<Celda>> mallaSiguiente; // Para no sobreescribir datos mientras calculamos

    float alpha; // Coeficiente de conductividad térmica

public:
    // Constructor: Inicializa la malla con un tamaño y temperatura base (ej. 20.0 °C)
    MallaTermica(int f, int c, float tempBase = 20.0f, float cond = 0.1f) 
        : filas(f), columnas(c), alpha(cond) {
        
        Celda celdaInicial = {tempBase, false};
        mallaActual = std::vector<std::vector<Celda>>(filas, std::vector<Celda>(columnas, celdaInicial));
        mallaSiguiente = mallaActual;
    }

    // Método para insertar un punto de calor fijo
    void aplicarCalor(int f, int c, float temp) {
        if (f >= 0 && f < filas && c >= 0 && c < columnas) {
            mallaActual[f][c].temperatura = temp;
            mallaActual[f][c].esFuente = true;
            mallaSiguiente[f][c] = mallaActual[f][c];
        }
    }

    // El corazón del simulador: Diferencias Finitas
    void actualizar() {
        // Recorremos las celdas internas (evitando los bordes para no salirnos de la matriz)
        for (int i = 1; i < filas - 1; ++i) {
            for (int j = 1; j < columnas - 1; ++j) {
                
                // Si es un punto de calor colocado por el usuario, no se enfría solo
                if (mallaActual[i][j].esFuente) {
                    continue; 
                }

                // Ecuación del calor discreta (Vecinos: arriba, abajo, izquierda, derecha)
                float tActual     = mallaActual[i][j].temperatura;
                float tArriba     = mallaActual[i-1][j].temperatura;
                float tAbajo      = mallaActual[i+1][j].temperatura;
                float tIzquierda  = mallaActual[i][j-1].temperatura;
                float tDerecha    = mallaActual[i][j+1].temperatura;

                // Calcular nueva temperatura basada en la diferencia con sus vecinos
                mallaSiguiente[i][j].temperatura = tActual + alpha * (tArriba + tAbajo + tIzquierda + tDerecha - 4.0f * tActual);
            }
        }
        // Copiamos el estado nuevo al actual para el siguiente frame
        mallaActual = mallaSiguiente;
    }

    // Getters para poder leer los datos desde OpenGL después
    int getFilas() const { return filas; }
    int getColumnas() const { return columnas; }
    float getTemperatura(int f, int c) const { return mallaActual[f][c].temperatura; }

    // ¡Aquí adentro debe estar! Método para aplanar la matriz para la GPU
    std::vector<float> obtenerDatosPlanos() const {
        std::vector<float> datos;
        datos.reserve(filas * columnas);
        
        for (int i = 0; i < filas; ++i) {
            for (int j = 0; j < columnas; ++j) {
                // Normalizamos el valor para el Shader (asumiendo min 20°C y max 100°C)
                float tMin = 20.0f;
                float tMax = 100.0f;
                float tNormalizada = (mallaActual[i][j].temperatura - tMin) / (tMax - tMin);
                
                // Asegurar que quede estrictamente entre 0 y 1
                if (tNormalizada < 0.0f) tNormalizada = 0.0f;
                if (tNormalizada > 1.0f) tNormalizada = 1.0f;

                datos.push_back(tNormalizada);
            }
        }
        return datos;
    }
}; // <-- La llave de cierre de la clase AHORA está aquí, al final de todo.

#endif