; Tauri NSIS installer hooks: this app ships its own core and daemon and runs the
; "Deskflow" service itself (see windows/deskflow-service.ps1).

!macro DESKFLOW_SERVICE ACTION
  nsExec::ExecToLog 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\setup\deskflow-service.ps1" -Action ${ACTION} -InstallDir "$INSTDIR"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "No se pudo configurar el servicio de Deskflow (código $0).$\r$\nDeskflow igual puede funcionar sin el servicio, pero no en la pantalla de inicio de sesión."
  ${EndIf}
!macroend

!macro NSIS_HOOK_PREINSTALL
  ; an upgrade replaces the running daemon and core: stop them so their files aren't locked
  nsExec::ExecToLog 'sc.exe stop Deskflow'
  Pop $0
  Sleep 2000
  nsExec::ExecToLog 'taskkill.exe /F /IM deskflow-core.exe'
  Pop $0
  nsExec::ExecToLog 'taskkill.exe /F /IM deskflow-daemon.exe'
  Pop $0
!macroend

!macro NSIS_HOOK_POSTINSTALL
  !insertmacro DESKFLOW_SERVICE Install
!macroend

!macro NSIS_HOOK_PREUNINSTALL
  !insertmacro DESKFLOW_SERVICE Uninstall
!macroend
