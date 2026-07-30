; ============================================================
;  Uncertain Master - Installeur VST3 (Windows 64-bit)
;  Installe le plugin dans le dossier standard VST3 que
;  FL Studio et Mixcraft scannent automatiquement.
;  Parametres passes par le workflow GitHub :
;    /DAppName= /DAppVersion= /DSrcDir= /DOutName= /DOutDir=
; ============================================================

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Uncertain
AppPublisherURL=https://uncertain.fr
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename={#OutName}
OutputDir={#OutDir}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes

[Files]
; Installe le bundle .vst3 complet dans C:\Program Files\Common Files\VST3
Source: "{#SrcDir}\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
  Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
SetupWindowTitle=Installation - {#AppName}
FinishedLabel=Uncertain Master est installe.%n%nDans FL Studio ou Mixcraft : rescanne tes plugins VST3, puis cherche "Uncertain Master".

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    Log('VST3 installe dans: ' + ExpandConstant('{commoncf64}\VST3\{#AppName}.vst3'));
end;
