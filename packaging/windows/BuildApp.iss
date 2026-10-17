; Inno Setup script for the Windows installer (VST3).
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.0 /DArtefacts=<repo>\build\BuildApp_artefacts\Release /O<out-dir> packaging\windows\BuildApp.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\BuildApp_artefacts\Release"
#endif

[Setup]
AppId={{098F55B1-634D-40C5-98DB-64A18440B038}
AppName=BuildApp
AppVersion={#AppVersion}
AppPublisher=Jonas Grunau
DefaultDirName={commoncf64}\VST3\BuildApp.vst3
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=BuildApp-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Files]
Source: "{#Artefacts}\VST3\BuildApp.vst3\*"; DestDir: "{commoncf64}\VST3\BuildApp.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
