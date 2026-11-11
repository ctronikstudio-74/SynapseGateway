

Sviluppare un progetto in C++20 utilizzando Kiro (l'IDE agentico basato su VS Code) è un'ottima scelta. 
Poiché Kiro eredita la compatibilità con l'ecosistema delle estensioni di VS Code e del registro Open VSX, puoi configurare un ambiente di sviluppo moderno, potente e potenziato dall'intelligenza artificiale per la Spec-Driven Development.

Kiro

1. Setup dell'Ambiente di Sviluppo (Prerequisiti di Sistema)
Prima di configurare Kiro, assicurati di avere installati sul tuo sistema gli strumenti di compilazione essenziali:

Compilatore C++ compatibile con C++20:

Windows: MSVC (tramite Visual Studio Build Tools) oppure MinGW-w64 (GCC).   
Kiro

macOS: Clang (incluso in Xcode Command Line Tools: xcode-select --install).   
Kiro

Linux: GCC (
≥11) o Clang (
≥13).   
DataCamp

CMake: Il sistema di build standard de facto per il C++ moderno.

Debugger: GDB (su Linux/Windows) o LLDB/CodeLLDB (su macOS/Windows).

2. Le Estensioni da Installare in Kiro
Essendo Kiro compatibile con le estensioni standard, puoi installarle direttamente dal pannello delle estensioni (Ctrl+Shift+X o Cmd+Shift+X):

C/C++ (di Microsoft) o clangd:

Consigliato: clangd al posto dell'IntelliSense di Microsoft per un autocompletamento più rapido, un'analisi semantica precisa e un LSP (Language Server Protocol) eccellente. (Se usi clangd, disabilita l'IntelliSense dell'estensione Microsoft per evitare conflitti).

CMake Tools (di Microsoft):

Indispensabile per configurare, compilare, testare e debuggare progetti basati su CMake direttamente dalla barra di stato di Kiro.

Error Lens:   
note

Mostra gli errori di compilazione e i warning direttamente inline accanto alla riga di codice, accelerando il debugging.   
note

CodeLLDB:

Un eccellente debugger basato su LLDB, perfetto per l'ispezione di variabili, breakpoint e memoria nei programmi C++.

3. Configurazione del Progetto C++20 con CMake
Crea una cartella per il tuo progetto e imposta una struttura pulita basata su CMake.

Crea un file CMakeLists.txt nella root del progetto configurato per C++20:

CMake
cmake_minimum_required(VERSION 3.20)
project(MioProgetto CXX)

# Imposta rigorosamente lo standard C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opzionale ma consigliato: attiva i warning di compilazione
if (MSVC)
    add_compile_options(/W4 /wx)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Aggiungi il tuo eseguibile
add_executable(MioProgetto main.cpp)
Crea un file main.cpp di prova sfruttando alcune feature di C++20 (come i Concepts o i Ranges):

C++
#include <iostream>
#include <concepts>

// Esempio di C++20 Concept
template<typename T>
requires std::integral<T>
T somma(T a, T b) {
    return a + b;
}

int main() {
    std::cout << "Benvenuto in C++20 con Kiro!" << std::endl;
    auto risultato = somma(10, 20);
    std::cout << "Risultato: " << risultato << std::endl;
    return 0;
}
4. Sfruttare Kiro per lo Sviluppo C++20
Kiro non è un semplice editor, ma un IDE agentico. Sfrutta le sue funzionalità native per scrivere codice C++ migliore:   
Kiro

Usa le Specs (Spec-Driven Development): Invece di scrivere codice a tentativi (vibe coding), usa la modalità Spec di Kiro per pianificare moduli, classi o la struttura logica del tuo software C++ prima che l'agente scriva il codice.   
Tom's Hardware
+ 1

Crea file di Steering (.kiro/steering/): Definisci un file di contesto persistente nel progetto per indicare a Kiro le tue preferenze di stile in C++20. Ad esempio, puoi istruire l'AI a:   
Kiro
+ 1

Privilegiare l'uso di std::ranges e std::views anziché i vecchi loop grezzi.

Utilizzare i concepts per vincolare i template.

Seguire le linee guida delle Core Guidelines di Bjarne Stroustrup.

Integrazione con il Terminale: Kiro può eseguire automaticamente i comandi di compilazione (cmake --build build) tramite terminale e interpretare eventuali errori del compilatore (es. errori di template complessi in C++20), aiutandoti a correggerli in automatico.   
DataCamp
+ 1

Hai già in mente che tipo di applicazione o libreria C++20 vuoi sviluppare con Kiro?