#version 330 core
layout (location = 0) in vec2 posicion; 
layout (location = 1) in vec2 coordTextura; 

out vec2 TexCoords;

uniform mat4 transformacion; 

void main() {
    gl_Position = transformacion * vec4(posicion, 0.0, 1.0);
    
    // CORRECCIÓN GRÁFICA: Invertimos el eje Y de la textura (1.0 - V)
    // Esto voltea la imagen verticalmente para que la fila 0 de C++ quede arriba en la pantalla
    TexCoords = vec2(coordTextura.x, 1.0f - coordTextura.y); 
}