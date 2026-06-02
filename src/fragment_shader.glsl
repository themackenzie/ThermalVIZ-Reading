#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D texturaTermica;

void main() {
    // 1. Captura de los datos puros del píxel actual
    vec3 datosTextura = texture(texturaTermica, TexCoords).rgb;
    float temperaturaCelda = datosTextura.r; // R = Calor normalizado (0.0 a 1.0)
    float mascaraMuro      = datosTextura.g; // G = Máscara del muro (1.0 = Muro)

    // Tamaño de un píxel en el espacio de la textura para buscar vecinos
    vec2 texelSize = 1.0 / textureSize(texturaTermica, 0);

    // =========================================================================
    // 2. RENDERIZADO DEL MURO ESTRUCTURAL (CON BISEL)
    // =========================================================================
    if (mascaraMuro > 0.5) { 
        // Muestreamos vecinos para detectar si es un borde del bloque
        float mArriba    = texture(texturaTermica, TexCoords + vec2(0.0, texelSize.y)).g;
        float mAbajo     = texture(texturaTermica, TexCoords + vec2(0.0, -texelSize.y)).g;
        float mIzquierda = texture(texturaTermica, TexCoords + vec2(-texelSize.x, 0.0)).g;
        float mDerecha   = texture(texturaTermica, TexCoords + vec2(texelSize.x, 0.0)).g;

        bool esBorde = (mArriba < 0.5 || mAbajo < 0.5 || mIzquierda < 0.5 || mDerecha < 0.5);
        
        if (esBorde) {
            FragColor = vec4(0.2, 0.2, 0.2, 1.0); // Borde oscuro definido
        } else {
            FragColor = vec4(0.35, 0.35, 0.35, 1.0); // Interior del bloque
        }
        return; 
    }

    // =========================================================================
    // 3. DETECTOR DE PROXIMIDAD AL MURO (EFECTO ANILLO DE CHOQUE)
    // =========================================================================
    // Muestreamos un anillo extendido (2 píxeles a la redonda) para detectar la cercanía del muro
    float proximidadMuro = 0.0;
    
    // Revisamos cruz inmediata (distancia 1)
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(0.0, texelSize.y)).g;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(0.0, -texelSize.y)).g;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(-texelSize.x, 0.0)).g;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(texelSize.x, 0.0)).g;

    // Revisamos esquinas diagonales (distancia 1.4)
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(-texelSize.x, texelSize.y)).g * 0.7;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(texelSize.x, texelSize.y)).g * 0.7;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(-texelSize.x, -texelSize.y)).g * 0.7;
    proximidadMuro += texture(texturaTermica, TexCoords + vec2(texelSize.x, -texelSize.y)).g * 0.7;

    // =========================================================================
    // 4. ATENUACIÓN Y AMOLDADO GRÁFICO DEL DEGRADADO
    // =========================================================================
    float factorAmortiguacion = 1.0;
    
    if (proximidadMuro > 0.1) {
        // Cuanto más cerca está del muro (mayor proximidadMuro), más drásticamente
        // reducimos el factor, forzando a la temperatura a decaer a cero (ambiente frío)
        factorAmortiguacion = clamp(1.0 - (proximidadMuro * 0.22), 0.0, 1.0);
    }

    // Aplicamos la amortiguación geométrica directamente sobre el calor de la celda
    float tCalibrada = temperaturaCelda * factorAmortiguacion;

    // Suavizado local para homogeneizar el fluido térmico en el aire libre
    float tArriba    = texture(texturaTermica, TexCoords + vec2(0.0, texelSize.y)).r;
    float tAbajo     = texture(texturaTermica, TexCoords + vec2(0.0, -texelSize.y)).r;
    float tIzquierda = texture(texturaTermica, TexCoords + vec2(-texelSize.x, 0.0)).r;
    float tDerecha   = texture(texturaTermica, TexCoords + vec2(texelSize.x, 0.0)).r;
    
    float tSuave = (tCalibrada * 0.36) + (tArriba + tAbajo + tIzquierda + tDerecha) * 0.16 * factorAmortiguacion;
    
    // Curva de amplificación de zonas de alto calor (0.6 para ensanchar núcleos)
    float temperaturaFinal = smoothstep(0.0, 0.6, clamp(tSuave, 0.0, 1.0));

    // =========================================================================
    // 5. ASIGNACIÓN DE LA PALETA TÉRMICA REAL
    // =========================================================================
    vec3 colorFrio     = vec3(0.0, 0.0, 0.4);   // Azul ambiente (Frío)
    vec3 colorMedio    = vec3(0.0, 0.9, 0.2);   // Verde intermedio
    vec3 colorCalido   = vec3(1.0, 0.6, 0.0);   // Naranja expansivo
    vec3 colorCaliente = vec3(1.0, 0.0, 0.0);   // Rojo Núcleo (400°C)

    vec3 colorFinal;
    if (temperaturaFinal < 0.33) {
        colorFinal = mix(colorFrio, colorMedio, temperaturaFinal * 3.0);
    } else if (temperaturaFinal < 0.66) {
        colorFinal = mix(colorMedio, colorCalido, (temperaturaFinal - 0.33) * 3.0);
    } else {
        colorFinal = mix(colorCalido, colorCaliente, (temperaturaFinal - 0.66) * 3.0);
    }

    FragColor = vec4(colorFinal, 1.0);
}