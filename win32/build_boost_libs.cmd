@echo off
rem Launch this file in the boost/ folder.

rem Check if we're in the correct folder
if not exist libs goto :error
if not exist boost goto :error
if not exist bjam.exe goto :error

b2 toolset=msvc variant=release,debug link=static threading=multi runtime-link=static stage /boost/chrono /boost/date_time /boost/filesystem /boost/program_options /boost/regex /boost/system /boost/thread

cd stage
start .

goto :success

:error
echo Please launch this file in the boost folder you downloaded and extracted from sourceforge.
echo Make sure you've run the bootstrap.bat file to create the bjam binary first.
echo 
echo Example: 
echo   cd c:\Downloads\boost_1_55_0\
echo   c:\Games\Doom3\darkmod_src\win32\build_boost_libs.cmd
goto :eof

:success
echo Successfully built the libraries, please copy them to the win32/lib/ folder now.