# Programowanie asynchroniczne w C++ - Szkolenie

## Konfiguracja środowiska

Proszę wybrać jedną z poniższych opcji:

### Lokalna

Przed szkoleniem należy zainstalować:

#### Kompilator C++:
  * GCC - Linux lub WSL
    * gcc (wersja >= 14)
    * [CMake > 3.25](https://cmake.org/)
      * proszę sprawdzić wersję w linii poleceń        
  
        ```
        cmake --version
        ```
    * vcpkg
      * instalacja - https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-bash
        * zklonować repozytorium vcpkg
          ```
          git clone https://github.com/microsoft/vcpkg.git
          ```
        * uruchomić skrypt bootstrap-vcpkg.sh
          ```
          cd vcpkg && ./bootstrap-vcpkg.sh
          ``` 
        * dodać zmienną środowiskową VCPKG_ROOT
          * w pliku `.bashrc` należy dodać wpis
          ```
          export VCPKG_ROOT=/path/to/vcpkg
          export PATH=$VCPKG_ROOT:$PATH
          ```        
        * odświeżyć zmienne środowiskowe
          ```
          source ~/.bashrc
          ```
        * zainstalować bibliotekę Catch2
          ```
          vcpkg install catch2
          ```
    * IDE: Visual Studio Code
      * [Visual Studio Code](https://code.visualstudio.com/)
      * zainstalować wtyczki
        * C/C++ Extension Pack
        * Live Share

### Docker + Visual Studio Code

Jeśli uczestnicy szkolenia korzystają w pracy z Docker'a, to należy zainstalować:

#### Docker Desktop (Windows)

* https://www.docker.com/products/docker-desktop/

#### Visual Studio Code

* [Visual Studio Code](https://code.visualstudio.com/)
* Zainstalować wtyczki
  * Live Share
  * Dev Containers ([wymagania](https://code.visualstudio.com/docs/devcontainers/containers#_system-requirements))
    * po instalacji wtyczki - należy otworzyć w VS Code folder zawierający sklonowane repozytorium i
      z palety poleceń (Ctrl+Shift+P) wybrać opcję **Dev Containers: Rebuild and Reopen in Container**