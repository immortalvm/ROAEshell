/*
    libroae - Simple management of ROAE files 

    Immortal Database Access (iDA) EUROSTARS project

    Eladio Gutierrez, Sergio Romero, Oscar Plata
    University of Malaga, Spain

    May 2023
*/

#include "roae.h"
static int main_roae(int argc, char* argv[])
{
    char *roaefile = "data/simpledb.roae";
    if (argc > 1) {
        roaefile = argv[1];
    }
    extern void ROAE_test(char*);
    ROAE_test(roaefile);
    return 0;
}

int main(int argc, char* argv[])
{
    int ret;
    //ret = main_siard(argc, argv);
    ret = main_roae(argc, argv);
    return ret;
}
