@ECHO OFF

:: Code to easily build a CIA from the 3DSX Output File using CXITOOL

:: ---- Make Commands ----
:: Runs the default makefile to create a .3dsx & .smdh file

@make

echo Built ... when-you-miss-me.elf
echo Built ... when-you-miss-me.3dsx
echo Built ... when-you-miss-me.smdh

:: ---- Cxitool Commands ----
:: Builds a .cxi file used for easy compilation into .cia

.\cxitool\cxitool ^
when-you-miss-me.3dsx ^
when-you-miss-me.cxi ^
--name=missme ^
--banner=banner.bnr

echo Built ... when-you-miss-me.cxi

:: ---- Makerom Commands ----
:: Builds a .cia file using the cxi file

.\makerom\makerom ^
-f cia ^
-o when-you-miss-me.cia ^
-i when-you-miss-me.cxi:0:0 ^
-ignoresign

echo Built ... when-you-miss-me.cia

:: ---- Servefiles Confirmation ----
:: Allows you to serve the cia file to FBI
set /p servefile="Would you like to send the built file to FBI on your 3DS? (Y/N)"
if "%servefile%"=="N" goto :eof
echo Open the FBI Homebrew Software on your 3DS System and hit 'Remote Install', then hit 'Recieve URLs over the network'
".\servefile\start.bat" when-you-miss-me.cia