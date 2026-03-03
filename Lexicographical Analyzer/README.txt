Phase 1 - Lexicographical Analysis

Authors:
    Mixalis Metzogiannakis - csd3998
    Spiros Papadomanolakis - csd5108
    Giannis Manassakis     - csd5277

How to run:
    1) Type make
    2) Type ./scanner --insert test file--
    3) You can also type make clean to delete executable "scanner" and source code "scanner.c"
Ti den ulopoihthike / comments:
1. I sinartisi malloc dimiourgei string size 700 me to int max_string_size = 700; , den eixame xrono 
na to kanoume na kanei realloc otan to megethos tou string kseperna ta 700, gia logous testing mporei na allaxtei se oso 
einai to max size sto test file 
2. I sinartisi gia ta multiline comment den pianei ta comments opws /* /* ok */ ws "/* ok " kai petaei error oti den ekleise
3. Stin ilopoihsh xrhsimopoihsame arketo kwdika apo tin persini mou prwti fasi (csd3998) kathws imoun omada monos 
kai den perasa to mathima wstoso apla xrhsimopoihsame ton idio kwdika mainly se oti itan na ilopoihsw egw gia tin omada fetos

