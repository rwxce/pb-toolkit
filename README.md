# PBToolkit

**PBToolkit** is a PowerBuilder analysis and extraction toolkit designed to perform **large-scale, automated extraction of PowerBuilder libraries (PBLs)** and generate structured, reusable artifacts for analysis and documentation.

The project is built around the open-source tool
[pb-pbldump](https://github.com/amoskovsky/pb-pbldump) by *amoskovsky*, which is used as the core extraction engine.

PBToolkit exists because native PowerBuilder tools such as **ORCA / ORCAScript** are limited to newer versions.

---

## 🧩 Features

* **Mass PBL extraction**
  Automates extraction of PowerBuilder objects across multiple PB versions using `pb-pbldump`.

* **Multi-version support**
  Designed to work with legacy and modern PowerBuilder versions (configurable).

* **Local mirror workflow**
  Creates a local mirror of PowerBuilder projects to allow safe, repeatable processing.

* **Structured output pipeline**
  Generates:

  * Raw extracted sources
  * Consolidated source files
  * SQL SELECT usage analysis
  * Data table mappings
  * Project dependency metadata

* **Extensible Python post-processing**
  Includes a Python pipeline that can be easily extended with additional scripts.

* **CLI-based interactive menu**
  Provides a console menu to run the full pipeline or individual steps.

---

## 🚀 Build and Execution

### 1. Build the project

Build the project using **Visual Studio** (x64, Debug or Release).

### 2. Run without Visual Studio (recommended)

To run PBToolkit **without opening the IDE**:

1. Build the project once.
2. Move the executable:

```
PBToolkit/x64/Debug/PBToolkit.exe
```

to the same directory level as `main.cpp`.

This allows the tool to resolve paths correctly and run without Visual Studio.

> ⚠️ If you modify the C++ code, you must rebuild and repeat this step.

---

## ⚙️ Configuration

### PowerBuilder versions

Supported PowerBuilder versions are defined in **`Config.h`**:

```cpp
static const inline std::wstring V65  = L"6.5";
static const inline std::wstring V7   = L"7.0";
static const inline std::wstring V8   = L"8.0";
static const inline std::wstring V9   = L"9.0";
static const inline std::wstring V105 = L"10.5";
static const inline std::wstring V125 = L"12.5";
```

You may freely add or remove versions as needed.

> If you change the supported versions, you must also update:
>
> * `MirrorManager.cpp`
> * `MenuHandler.cpp`
> * `PblScanner.cpp`

---

### PowerBuilder source root

In **`Config.cpp`**, you must define:

```cpp
const fs::path Config::PB_ROOT = fs::path{ LR"(TODO)" };
```

This path must point to the directory where your PowerBuilder projects
(PBLs, PBTs, source folders) are stored.

PBToolkit will create a **local mirror** from this location for processing.

---

### Mirror directory

By default, the local mirror is created at:

```
C:\Users\Public\Documents\PBToolkit\mirror
```

You can change this path in **`Config.cpp`** if needed.

---

## 🐍 Python Pipeline

PBToolkit includes a Python post-processing pipeline managed by `PythonRunner`.

### Adding new Python scripts

1. Create the script inside:

```
PBToolkit/Source Files/Python
```

2. Register it in `PythonRunner.cpp` inside the `getPythonScripts()` function.
3. If new dependencies are required, update:

```
PBToolkit/Source Files/Python/requirements.txt
```

The virtual environment is automatically created and managed by PBToolkit.

---

## 📂 Project Structure (simplified)

```
PBToolkit/
├── main.cpp
├── PBToolkit.exe
├── PBToolkit.vcxproj
│
├── Header Files/
│   ├── Config.h
│   ├── Logger.h
│   ├── MenuHandler.h
│   ├── MirrorManager.h
│   ├── PblScanner.h
│   ├── PythonRunner.h
│   ├── ScriptGenerator.h
│   └── Utils.h
│
├── Source Files/
│   ├── Main/
│   │   ├── Config.cpp
│   │   ├── Logger.cpp
│   │   ├── MenuHandler.cpp
│   │   ├── MirrorManager.cpp
│   │   ├── PblScanner.cpp
│   │   ├── PythonRunner.cpp
│   │   ├── ScriptGenerator.cpp
│   │   └── Utils.cpp
│   │
│   └── Python/
│       ├── combine_to_files.py
│       ├── extract_pbt_dependencies.py
│       ├── extract_selects.py
│       ├── extract_table_values.py
│       ├── summarize_selects.py
│       └── requirements.txt
│
└── Resource Files/
    └── Libraries/
        └── pbldump-1.3.1stable/
            └── PblDump.exe
```

---

## 🧪 Intended Use

PBToolkit is intended for:

* Legacy PowerBuilder system analysis
* Large-scale codebase auditing
* Dependency and SQL usage inspection
* Documentation and migration support
* Research and tooling around PowerBuilder internals

It is **not** a runtime tool and does not execute application logic.

---

## 🧾 License

This project is licensed under the **MIT License**.
See the [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

Contributions are welcome.

Feel free to:

* Fork the project
* Improve extraction logic
* Add new analysis scripts
* Improve documentation

This project is intentionally modular and designed to be extended.