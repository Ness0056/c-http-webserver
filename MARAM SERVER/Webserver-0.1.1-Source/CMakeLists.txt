 project(Webserver) # Der Name unseres Projekts
  cmake_minimum_required(VERSION 3.5) # Minimale CMake-Version 3.5
  set(CMAKE_C_STANDARD 11) # Wir verwenden C-Version C11

  # Diese Zeile ist die Anweisung, dass wir eine ausführbare Datei erstellen wollen
  add_executable(webserver Webserver.c)


 # Wenn Sie mehrere Code-Dateien haben, werden diese Leerzeichen getrennt angehängt:
 # add_executable(executable first.c second.c third.c)

 # Diese Zeilen sind für das Erstellen der Abgabedatei relevant
 set(CPACK_SOURCE_GENERATOR "TGZ") # Abgabe soll als .tar.gz erstellt werden
 # Die fertige Abgabe enthält nur den Quellcode und nicht ihr Build-Verzeichnis
 set(CPACK_SOURCE_IGNORE_FILES ${CMAKE_BINARY_DIR} /\\..*$)
 set(CPACK_VERBATIM_VARIABLES YES) # Variablen sollen nicht optimiert werden
 include(CPack) # Wir nutzen CPack um das Archiv zu erstellen
