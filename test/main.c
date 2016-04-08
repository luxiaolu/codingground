#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Song {
  char name[128];
  char Atist[64];
};

typedef struct Song Song;

int main()
{
    Song a;
    strcpy(a.name, "testtest");
    printf(a.name);
    return 0;
}

