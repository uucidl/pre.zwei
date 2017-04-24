set OutputDir=H:\pre.zwei\builds
set CLFlags=-nologo -Gm- -GR- -EHa- -Z7 -FC -EHsc -DEBUG
set CLFlags=%CLFlags% -Fo%OutputDir%\obj\
set CLFlags=%CLFlags% -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -wd4127
set CLFlags=%CLFlags% -wd4477 -wd4838 -wd4018 -wd4244 -wd4456 -wd4457
mkdir %OutputDir%\obj
cd modules\hammer
call scons DESTDIR=%OutputDir% prefix="/" install
cd ..\..
set CLExecutableOutput=%OutputDir%\zwei_app.dll
cl %CLFlags% /DLL -DZWEI_API_DLL=zw_dll_exported ^
    -DZWEI_SLOW=1 ^
    -DZWEI_INTERNAL=1 ^
    src\zwei_lib_win32_unit.cpp ^
    src\spdr_unit.cpp ^
    %OutputDir%\lib\hammer_s.lib ^
    -Fe%CLExecutableOutput% ^
    -Fm%OutputDir%\zwei_app.map ^
    -LD /link -incremental:no -opt:ref -PDB:%OutputDir%\zwei_app.pdb
@echo SHARED_LIBRARY %CLExecutableOutput%

set CLExecutableOutput=%OutputDir%\zwei.exe
cl %CLFlags% ^
    src\zwei_main_win32_unit.cpp ^
    src\spdr_unit.cpp ^
    -DZWEI_SLOW=1 ^
    -DZWEI_INTERNAL=1 ^
    -Fe%CLExecutableOutput%
@echo PROGRAM %CLExecutableOutput%

set CLExecutableOutput=%OutputDir%\tests.exe
cl %CLFlags% ^
    src\tests\tests_unit.cpp ^
    -DZWEI_SLOW=1 ^
    -DZWEI_INTERNAL=1 ^
    -Fe%CLExecutableOutput%
@echo PROGRAM %CLExecutableOutput%

@REM run those tests immediately
%CLExecutableOutput%