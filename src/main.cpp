#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <random> // Para std::random_device y std::mt19937

#include <glad/glad.h>
#include <GLFW/glfw3.h>

// Cabeceras de Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Incluimos GLM para las matrices de transformación (Zoom)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SimuladorQuadtree.hpp"

// --- VARIABLES GLOBALES PARA CONTROL DE CÁMARA E INTERACCIÓN ---
float escalaZoom = 1.0f; 
float offsetX = 0.0f;    
float offsetY = 0.0f;    
double ultimoX = 0.0;
double ultimoY = 0.0;
bool arrastrando = false; 

int ANCHO_MALLA = 512; // Alta resolución activa
int ALTO_MALLA = 512;

// --- VARIABLES DE CONTROL PARA IMGUI ---
float temperaturaPincel = 100.0f; // Control dinámico de calor



// 1. DEFINICIÓN DE MODOS DE TRABAJO
enum ModoSimulador {
    MODO_BORRADOR,
    MODO_PLANO,      // Para dibujar paredes aislantes
    MODO_SIMULACION  // Para inyectar calor y correr la física
};

// Variables globales o estáticas de control
ModoSimulador modoActual = MODO_PLANO;
bool simulacionPausada = true; // Por defecto arranca en pausa para poder dibujar tranquilos


// --- FUNCIONES AUXILIARES PARA SHADERS ---
std::string leerShader(const char* ruta) {
    std::ifstream archivo(ruta);
    std::stringstream strStream;
    strStream << archivo.rdbuf();
    return strStream.str();
}

GLuint compilarShader(const char* ruta, GLenum tipo) {
    std::string codigoStr = leerShader(ruta);
    const char* codigo = codigoStr.c_str();
    GLuint shader = glCreateShader(tipo);
    glShaderSource(shader, 1, &codigo, NULL);
    glCompileShader(shader);
    return shader;
}

void configurar_viewport(GLFWwindow* ventana, int ancho, int alto) {
    glViewport(0, 0, ancho, alto);
}

void callback_scroll(GLFWwindow* ventana, double xoffset, double yoffset) {
    // Si el mouse está sobre una ventana de ImGui, no hacemos zoom en la simulación
    if (ImGui::GetIO().WantCaptureMouse) return;

    escalaZoom += yoffset * 0.1f;
    if (escalaZoom < 1.0f) escalaZoom = 1.0f;
    if (escalaZoom > 10.0f) escalaZoom = 10.0f;
}

void procesar_entrada(GLFWwindow* ventana, SimuladorQuadtree& simulador, const std::vector<float>& datosPlano) {
    if (glfwGetKey(ventana, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(ventana, true);
    }

    // ¡CRÍTICO! Si ImGui está usando el mouse (moviendo el panel o el slider),
    // bloqueamos la entrada en la simulación para no pintar calor por detrás.
    if (ImGui::GetIO().WantCaptureMouse) return;

    int anchoVentana, altoVentana;
    glfwGetWindowSize(ventana, &anchoVentana, &altoVentana);

    // ==========================================
    // LÓGICA DE ARRASTRE (CLIC DERECHO)
    // ==========================================
    if (glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(ventana, &xpos, &ypos);

        if (!arrastrando) {
            ultimoX = xpos; ultimoY = ypos; arrastrando = true;
        } else {
            double deltaX = xpos - ultimoX; double deltaY = ypos - ultimoY;
            offsetX += (2.0f * deltaX) / anchoVentana;
            offsetY -= (2.0f * deltaY) / altoVentana;
            ultimoX = xpos; ultimoY = ypos;
        }
        float limiteX = escalaZoom - 1.0f; float limiteY = escalaZoom - 1.0f;
        if (offsetX < -limiteX) offsetX = -limiteX;
        if (offsetX > limiteX)  offsetX = limiteX;
        if (offsetY < -limiteY) offsetY = -limiteY;
        if (offsetY > limiteY)  offsetY = limiteY;
    } else {
        arrastrando = false;
    }

    // Variables estáticas para la trayectoria del mouse
    static double xAnteriorObjeto = 0.0;
    static double yAnteriorObjeto = 0.0;
    static bool primerClic = true;

    // =========================================================================
    // LÓGICA DE TRAZO UNIFICADA: PARED CUADRADA Y CALOR ADITIVO PROTEGIDO
    // =========================================================================
    if (glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(ventana, &xpos, &ypos);

        int anchoVentana, altoVentana;
        glfwGetWindowSize(ventana, &anchoVentana, &altoVentana);

        float xNdc = (2.0f * xpos) / anchoVentana - 1.0f;
        float yNdc = 1.0f - (2.0f * ypos) / altoVentana;

        float xActualObjeto = (xNdc - offsetX) / escalaZoom;
        float yActualObjeto = (yNdc - offsetY) / escalaZoom;

        if (primerClic) {
            xAnteriorObjeto = xActualObjeto;
            yAnteriorObjeto = yActualObjeto;
            primerClic = false;
        }

        float distanciaSalto = std::sqrt((xActualObjeto - xAnteriorObjeto) * (xActualObjeto - xAnteriorObjeto) + 
                                         (yActualObjeto - yAnteriorObjeto) * (yActualObjeto - yAnteriorObjeto));
        
        float espacioEntrePuntos = 0.004f; 
        int pasosInterpolacion = std::max(1, static_cast<int>(distanciaSalto / espacioEntrePuntos));

        for (int pasoIntermedio = 0; pasoIntermedio <= pasosInterpolacion; ++pasoIntermedio) {
            float t = static_cast<float>(pasoIntermedio) / pasosInterpolacion;
            
            float xIntermedio = xAnteriorObjeto + t * (xActualObjeto - xAnteriorObjeto);
            float yIntermedio = yAnteriorObjeto + t * (yActualObjeto - yAnteriorObjeto);

            if (xIntermedio < -1.0f || xIntermedio > 1.0f || yIntermedio < -1.0f || yIntermedio > 1.0f) continue;

            int centroJ = static_cast<int>((xIntermedio + 1.0f) * 0.5f * ANCHO_MALLA);
            int centroI = static_cast<int>((1.0f - yIntermedio) * 0.5f * ALTO_MALLA);

            int radioCeldas = 6; 

            for (int di = -radioCeldas; di <= radioCeldas; ++di) {
                for (int dj = -radioCeldas; dj <= radioCeldas; ++dj) {
                    
                    int i = centroI + di;
                    int j = centroJ + dj;

                    if (i >= 0 && i < ALTO_MALLA && j >= 0 && j < ANCHO_MALLA) {
                        
                        float celdaX = (static_cast<float>(j) / ANCHO_MALLA) * 2.0f - 1.0f;
                        float celdaY = 1.0f - (static_cast<float>(i) / ALTO_MALLA) * 2.0f;

                        // =========================================================================
                        // PROCESAMIENTO DE HERRAMIENTAS DE ALTA VELOCIDAD (ESTADO BOOLEANO)
                        // =========================================================================

                        if (modoActual == MODO_BORRADOR) {
                            // MODO 1: BORRADOR - Directo y rápido
                            simulador.limpiarCeldaCompleta(celdaX, celdaY);
                        }
                        else if (modoActual == MODO_PLANO) {
                            // MODO 2: CONSTRUIR MUROS - Tallado directo en el Quadtree usando esPared
                            simulador.tallarMuroEstructural(celdaX, celdaY);
                        }
                        else if (modoActual == MODO_SIMULACION) {
                        // Leemos el canal Rojo (índice 0 de cada píxel de 3 componentes) directamente de nuestro datosPlano
                        int indiceMatriz = (i * ANCHO_MALLA + j) * 3;
                        float tExistenteEnMatriz = datosPlano[indiceMatriz + 0]; // Canal R
                        float mascaraMuroExistente = datosPlano[indiceMatriz + 1]; // Canal G

                        // Si la celda es un muro gris, el pincel pasa de largo completamente
                        if (mascaraMuroExistente > 0.1) {
                            continue;
                        }

                        float diffX = celdaX - xIntermedio;
                        float diffY = celdaY - yIntermedio;
                        float distanciaCentroCuadrado = diffX * diffX + diffY * diffY;
                        
                        float radioPincelMundo = 0.08f; // Ajusta el radio a tu gusto para trazos más gruesos
                        float radioMundoCuadrado = radioPincelMundo * radioPincelMundo;

                        if (distanciaCentroCuadrado <= radioMundoCuadrado) {
                            float distanciaNormalizada = std::sqrt(distanciaCentroCuadrado) / radioPincelMundo;
                            // Aplicamos un gradiente suave invertido para el núcleo del pincel
                            float factorGradiente = 1.0f - distanciaNormalizada;
                            float tPropuesta = temperaturaPincel * factorGradiente;

                            // Convertimos el valor normalizado de la matriz de vuelta a Celsius para comparar correctamente
                            float tExistenteCelsius = tExistenteEnMatriz * 400.0f;

                            // Solo inyectamos calor si la nueva energía propuesta es mayor a la que ya existe.
                            // Esto evita que el trazo continuo del mouse "pise" o apague el calor anterior.
                            if (tPropuesta > tExistenteCelsius) {
                                simulador.aplicarCalor(celdaX, celdaY, tPropuesta);
                            }
                        }
                    }
                    }
                }
            }
        }

        xAnteriorObjeto = xActualObjeto;
        yAnteriorObjeto = yActualObjeto;
    } else {
        primerClic = true;
    }
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* ventana = glfwCreateWindow(1024, 768, "Simulador Termico Avanzado - Quadtree & ImGui", NULL, NULL);
    if (ventana == NULL) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(ventana);
    
    glfwSetFramebufferSizeCallback(ventana, configurar_viewport);
    glfwSetScrollCallback(ventana, callback_scroll);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { glfwTerminate(); return -1; }

    // INITIALIZAR CONTEXTO DE DEAR IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark(); // Estilo visual oscuro profesional

    // Vincular ImGui con los Backends de GLFW y OpenGL
    ImGui_ImplGlfw_InitForOpenGL(ventana, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint vertexShader = compilarShader("src/vertex_shader.glsl", GL_VERTEX_SHADER);
    GLuint fragmentShader = compilarShader("src/fragment_shader.glsl", GL_FRAGMENT_SHADER);
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    SimuladorQuadtree simulador(0.05f); 

    float vertices[] = {
        -1.0f,  1.0f,   0.0f, 1.0f,
        -1.0f, -1.0f,   0.0f, 0.0f,
         1.0f, -1.0f,   1.0f, 0.0f,

        -1.0f,  1.0f,   0.0f, 1.0f,
         1.0f, -1.0f,   1.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 1.0f
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // =========================================================================
    // CONFIGURACIÓN DE LA TEXTURA EN MAIN.CPP (INICIALIZACIÓN)
    // =========================================================================
    GLuint idTexturaTermica;
    glGenTextures(1, &idTexturaTermica);
    glBindTexture(GL_TEXTURE_2D, idTexturaTermica);

    // CRÍTICO: El tercer parámetro DEBE ser GL_RGB o GL_RGB8 para que la GPU
    // reserve espacio para los canales Rojo (Calor) y Verde (Muro) por separado.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, ANCHO_MALLA, ALTO_MALLA, 0, GL_RGB, GL_FLOAT, nullptr);

    // Filtros de interpolación estables
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "mapaTermico"), 0);

    glfwSwapInterval(1); 

    while (!glfwWindowShouldClose(ventana)) {
        glfwPollEvents();

        // 1. Iniciar el Frame de ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 2. CREAR LA VENTANA FLOTANTE DE CONTROL INTERACTIVO
        {
            // ==========================================
            // INTERFAZ DE USUARIO (DEAR IMGUI)
            // ==========================================
            ImGui::Begin("Controles del Simulador");

            ImGui::Text("SELECCIONAR HERRAMIENTA:");
            if (ImGui::RadioButton("Modo 1: Borrador (Muros y Calor)", modoActual == MODO_BORRADOR)) {
                modoActual = MODO_BORRADOR;
            }
            if (ImGui::RadioButton("Modo 2: Construir Muros (Gris)", modoActual == MODO_PLANO)) {
                modoActual = MODO_PLANO;
            }
            if (ImGui::RadioButton("Modo 3: Pincel Térmico", modoActual == MODO_SIMULACION)) {
                modoActual = MODO_SIMULACION;
            }

            ImGui::Separator();

            if (modoActual == MODO_SIMULACION) {
                ImGui::SliderFloat("Temperatura Pincel (C)", &temperaturaPincel, 0.0f, 400.0f, "%.1f");


                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // =========================================================================
                // GENERADOR DE PUNTOS TÉRMICOS ALEATORIOS
                // =========================================================================
                if (ImGui::Button("Generar Ráfaga de Focos")) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    
                    // Distribución uniforme para las posiciones espaciales (-1.0 a 1.0)
                    std::uniform_real_distribution<float> disPos(-1.0f, 1.0f);
                    
                    // Distribución uniforme auxiliar entre 0.0 y 1.0 para calcular el sesgo
                    std::uniform_real_distribution<float> disFactor(0.0f, 1.0f);

                    int cantidadPuntos = 45; // Cantidad de focos que se inyectarán de golpe

                    for (int k = 0; k < cantidadPuntos; ++k) {
                        float xAleatorio = disPos(gen);
                        float yAleatorio = disPos(gen);

                        // --- CÁLCULO DEL SESGO HACIA LOS 400°C ---
                        // Al elevar el factor a una potencia (ej. 0.4), los valores cercanos 
                        // a 1.0 se vuelven extremadamente dominantes matemáticamente.
                        float factorSesgo = std::pow(disFactor(gen), 0.4f); 
                        
                        // Mapeamos el factor al rango térmico deseado (entre 80°C y 400°C)
                        float tempAleatoria = 80.0f + (factorSesgo * (400.0f - 80.0f));

                        // Inyección directa en el Quadtree
                        simulador.aplicarCalor(xAleatorio, yAleatorio, tempAleatoria);
                    }
                }
            }

            ImGui::End();
        }


        {
            ImGui::SetNextWindowSize(ImVec2(400.0f, 440.0f), ImGuiCond_FirstUseEver);
            
            ImGui::Begin("Visor de Estructura Quadtree");
            
            ImGui::Text("Topología Dinámica con Zoom Sincronizado");
            ImGui::Text("Mueve/Amplía la escena principal para inspeccionar los nodos.");
            ImGui::Separator();

            ImVec2 espacioDisponible = ImGui::GetContentRegionAvail();
            if (espacioDisponible.y <= 0.0f) {
                espacioDisponible.y = ImGui::GetWindowSize().y - 80.0f;
            }

            float ladoCanvas = std::min(espacioDisponible.x, espacioDisponible.y);
            
            if (ladoCanvas > 10.0f) {
                ImGui::InvisibleButton("canvas_quadtree", ImVec2(ladoCanvas, ladoCanvas));
                
                ImVec2 posMin = ImGui::GetItemRectMin(); // Centro de dibujo en pantalla (Min)
                ImVec2 posMax = ImGui::GetItemRectMax(); // Centro de dibujo en pantalla (Max)

                ImDrawList* drawList = ImGui::GetWindowDrawList();

                // Fondo del canvas
                drawList->AddRectFilled(posMin, posMax, IM_COL32(10, 10, 10, 255));

                // Guardamos el área de recorte para que las líneas con zoom no se salgan del recuadro
                drawList->PushClipRect(posMin, posMax, true);

                // Recuperamos las cajas de los nodos y sus temperaturas asociadas
                // Para simplificar, expandiremos el vector de límites para que también nos devuelva la temperatura.
                // Modificaremos levemente el método del Simulador abajo para que mande 5 floats por nodo: [xMin, yMin, xMax, yMax, temp]
                std::vector<float> datosNodos = simulador.obtenerLimitesNodos();

                // Centro del canvas para aplicar las transformaciones lineales corporativas
                float centroCanvasX = posMin.x + ladoCanvas * 0.5f;
                float centroCanvasY = posMin.y + ladoCanvas * 0.5f;

                for (size_t k = 0; k < datosNodos.size(); k += 5) {
                    float xMin = datosNodos[k + 0];
                    float yMin = datosNodos[k + 1];
                    float xMax = datosNodos[k + 2];
                    float yMax = datosNodos[k + 3];
                    float temp = datosNodos[k + 4]; // Quinta posición: Temperatura o Pared (-1)

                    // 1. MAPEADO BASE: De rango mundo (-1.0 a 1.0) a píxeles locales del canvas
                    float pxMin = (xMin + 1.0f) * 0.5f * ladoCanvas;
                    float pxMax = (xMax + 1.0f) * 0.5f * ladoCanvas;
                    float pxMinY = (1.0f - yMax) * 0.5f * ladoCanvas;
                    float pxMaxY = (1.0f - yMin) * 0.5f * ladoCanvas;

                    // 2. APLICACIÓN DE MATRIZ DE TRANSFORMACIÓN (Zoom y Offset de la cámara principal)
                    // Convertimos el offset del mundo a píxeles proporcionales al tamaño del canvas
                    float pixelOffsetX = (offsetX * ladoCanvas) * 0.5f;
                    float pixelOffsetY = (-offsetY * ladoCanvas) * 0.5f; // Invertido por el eje de pantalla

                    // Coordenadas finales aplicando la escala de Zoom respecto al centro y los desplazamientos
                    float pantallaXMin = centroCanvasX + (pxMin - ladoCanvas * 0.5f) * escalaZoom + pixelOffsetX;
                    float pantallaXMax = centroCanvasX + (pxMax - ladoCanvas * 0.5f) * escalaZoom + pixelOffsetX;
                    float pantallaYMin = centroCanvasY + (pxMinY - ladoCanvas * 0.5f) * escalaZoom + pixelOffsetY;
                    float pantallaYMax = centroCanvasY + (pxMaxY - ladoCanvas * 0.5f) * escalaZoom + pixelOffsetY;

                    // 3. DETERMINAR EL COLOR DEL NODO SEGÚN SU NATURALEZA
                    ImU32 colorNodo = IM_COL32(0, 230, 70, 70); // Verde estándar para hojas frías
                    if (temp < -0.5f) {
                        colorNodo = IM_COL32(120, 120, 120, 150); // Gris si es un Muro Estructural
                    } else if (temp > 25.0f) {
                        // Cambia a tono rojizo/anaranjado si tiene carga térmica real
                        int intensidadRojo = std::min(255, static_cast<int>((temp / 400.0f) * 255));
                        colorNodo = IM_COL32(intensidadRojo, 100, 50, 120);
                    }

                    // Dibujamos el contorno del rectángulo
                    drawList->AddRect(ImVec2(pantallaXMin, pantallaYMin), ImVec2(pantallaXMax, pantallaYMax), colorNodo, 0.0f, 0, 1.0f);

                    // 4. IMPRESIÓN DE VALORES TEXTUALES (Solo si el tamaño del nodo en pantalla es lo bastante grande)
                    float anchoNodoPantalla = pantallaXMax - pantallaXMin;
                    if (anchoNodoPantalla > 35.0f) { // Evita sobrecargar la pantalla si hay demasiada densidad
                        char bufferTexto[16];
                        if (temp < -0.5f) {
                            sprintf(bufferTexto, "WALL");
                        } else {
                            sprintf(bufferTexto, "%.0fC", temp);
                        }

                        // Calcular el centro exacto del cuadro para colocar el texto
                        float textoX = pantallaXMin + (anchoNodoPantalla * 0.1f);
                        float textoY = pantallaYMin + ((pantallaYMax - pantallaYMin) * 0.35f);

                        // Texto blanco con una ligera transparencia
                        drawList->AddText(ImVec2(textoX, textoY), IM_COL32(255, 255, 255, 255), bufferTexto);
                    }
                }

                drawList->PopClipRect(); // Restauramos el área de dibujo libre
            }
            ImGui::End();
        }
        

        //3. Si la física no está pausada, actualizamos el Quadtree
        if (!simulacionPausada) {
            simulador.actualizar();
        }

        //4. Obtenemos la matriz plana del frame actual
        std::vector<float> datosPlano = simulador.obtenerMatrizPlana(ANCHO_MALLA, ALTO_MALLA);

        //5. Procesar interacciones del mouse de la simulación
        procesar_entrada(ventana, simulador, datosPlano);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, idTexturaTermica);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ANCHO_MALLA, ALTO_MALLA, GL_RGB, GL_FLOAT, datosPlano.data());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ANCHO_MALLA, ALTO_MALLA, 0, GL_RGB, GL_FLOAT, datosPlano.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::mat4 matrizTransformacion = glm::mat4(1.0f);
        matrizTransformacion = glm::translate(matrizTransformacion, glm::vec3(offsetX, offsetY, 0.0f));
        matrizTransformacion = glm::scale(matrizTransformacion, glm::vec3(escalaZoom, escalaZoom, 1.0f));

        GLint transformLoc = glGetUniformLocation(shaderProgram, "transformacion");
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(matrizTransformacion));

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 3. Renderizar los datos de interfaz de ImGui sobre OpenGL
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(ventana);
    }

    // LIMPIEZA DE CONTEXTOS
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &idTexturaTermica);
    glfwTerminate();
    return 0;
}

//g++ src/*.cpp src/glad.c -o programa.exe -Iinclude -Llib -lglfw3 -lopengl32 -lgdi32 -std=c++11