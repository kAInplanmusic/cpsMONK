#pragma once

// Measurement data – gespeichert im Flash‑Block
struct Measurement {
    uint32_t cps;   // Schläge pro Sekunde
    float    quality; // 0–100 %, Basis‑Qualität
    uint8_t  raw[48]; // Platzhalter für zusätzliche Rohdaten
};

#ifndef IMPACT_ANALYZER_H
#define IMPACT_ANALYZER_H

#endif
