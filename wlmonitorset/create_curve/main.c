/* how to compile: gcc main.c -lm -o create_curve */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

#define PROG_VER "1.1"

#define MAX_VAL 1000

int set_splines(char *, int n);
double get_value(double x, double t_array[], int n);


int main(int argc, char *argv[]) {
    int ret = 0;
    int opt;
    char ret_r[MAX_VAL];
    char ret_g[MAX_VAL];
    char ret_b[MAX_VAL];
    
    while((opt = getopt(argc, argv, "r:g:b:h")) != -1)  
    {  
        switch(opt)  
        {  
            case 'r':
                strcpy(ret_r,optarg);
                break;
            case 'g':
                strcpy(ret_g,optarg);
                break;
            case 'b':
                strcpy(ret_b,optarg);
                break;
            case 'h': case '?':
                printf("create_curve v. %s\n\
Help:\n\
\tThis program supports 3 or 5 or 8 values per colour channel.\n\
\tEach value is a decimal number from 0.0 to 1.0.\n\
\tExample with three values (per colour channel):\n\
\t./create_curve -r 0.0:0.5:1.0 -g 0.0:0.5:1.0 -b 0.0:0.5:1.0\n", PROG_VER);
                return 0;  
        }  
    }
    
    ret = set_splines(ret_r, 0);
    ret = set_splines(ret_g, 1);
    ret = set_splines(ret_b, 1);
    
    if (ret == 0) {
        printf("Done!\n");
    } else if (ret == -1) {
        printf("Error! Exiting...\n");
        return 0;
    }

    return 0;
    } 


double get_value(double x, double t_array[], int n) {
    
    int a,b,c,d,e,f,g,h;
    
    a = 0;
    b = 1;
    c = 2;
    d = 3;
    e = 4;
    f = 5;
    g = 6;
    h = 7;
    
    double xx;
    if ( n == 8 ) {
        // 8 values - 256/8 = 32 blocks of 8 elements each
        xx =  t_array[0]*(x-b)*(x-c)*(x-d)*(x-e)*(x-f)*(x-g)*(x-h) / ((a-b)*(a-c)*(a-d)*(a-e)*(a-f)*(a-g)*(a-h))
            + t_array[1]*(x-a)*(x-c)*(x-d)*(x-e)*(x-f)*(x-g)*(x-h) / ((b-a)*(b-c)*(b-d)*(b-e)*(b-f)*(b-g)*(b-h))
            + t_array[2]*(x-a)*(x-b)*(x-d)*(x-e)*(x-f)*(x-g)*(x-h) / ((c-a)*(c-b)*(c-d)*(c-e)*(c-f)*(c-g)*(c-h))
            + t_array[3]*(x-a)*(x-b)*(x-c)*(x-e)*(x-f)*(x-g)*(x-h) / ((d-a)*(d-b)*(d-c)*(d-e)*(d-f)*(d-g)*(d-h))
            + t_array[4]*(x-a)*(x-b)*(x-c)*(x-d)*(x-f)*(x-g)*(x-h) / ((e-a)*(e-b)*(e-c)*(e-d)*(e-f)*(e-g)*(e-h))
            + t_array[5]*(x-a)*(x-b)*(x-c)*(x-d)*(x-e)*(x-g)*(x-h) / ((f-a)*(f-b)*(f-c)*(f-d)*(f-e)*(f-g)*(f-h))
            + t_array[6]*(x-a)*(x-b)*(x-c)*(x-d)*(x-e)*(x-f)*(x-h) / ((g-a)*(g-b)*(g-c)*(g-d)*(g-e)*(g-f)*(g-h))
            + t_array[7]*(x-a)*(x-b)*(x-c)*(x-d)*(x-e)*(x-f)*(x-g) / ((h-a)*(h-b)*(h-c)*(h-d)*(h-e)*(h-f)*(h-g));
    } else if ( n == 5 ) {
        // 5 values
        xx =  t_array[0]*(x-b)*(x-c)*(x-d)*(x-e) / ((a-b)*(a-c)*(a-d)*(a-e))
            + t_array[1]*(x-a)*(x-c)*(x-d)*(x-e) / ((b-a)*(b-c)*(b-d)*(b-e))
            + t_array[2]*(x-a)*(x-b)*(x-d)*(x-e) / ((c-a)*(c-b)*(c-d)*(c-e))
            + t_array[3]*(x-a)*(x-b)*(x-c)*(x-e) / ((d-a)*(d-b)*(d-c)*(d-e))
            + t_array[4]*(x-a)*(x-b)*(x-c)*(x-d) / ((e-a)*(e-b)*(e-c)*(e-d));
    } else if ( n == 3 ) {
        // 3 values
        xx =  t_array[0]*(x-b)*(x-c) / ((a-b)*(a-c))
            + t_array[1]*(x-a)*(x-c) / ((b-a)*(b-c))
            + t_array[2]*(x-a)*(x-b) / ((c-a)*(c-b));
    }
    
    return xx;
}


int set_splines(char * values, int n) {
    
    char *t;
    double t_array[11];
    t = strtok(values,":");
    int j = 0;
    
    if (t != NULL) {
        t_array[0] = atof(t);
        ++j;
        
        while ( (t = strtok(NULL, ":")) != NULL ) {
            t_array[j] = atof(t);
            ++j;
        }
    }
    
    FILE *fp;
    if (n == 0) {
        fp = fopen("data_array","w");
        if (fp == NULL) {
            printf("Cannot create the file data_array.\n");
            return -1;
        }
    } else {
        fp = fopen("data_array","a");
        if (fp == NULL) {
            printf("Cannot create the file data_array.\n");
            return -1;
        }
    }
    
    
    double xx;
    
    double x;
    int i;
    if (j == 8) {
        for (i=0 ; i<256; ++i) {
            x = (7.0/255)*i;
            xx = get_value(x, t_array, 8);
            fprintf(fp, "%f", xx);
            if (i == 255) {
                fprintf(fp, "\n");
            } else {
                fprintf(fp, " ");
            }
        }
    } else if (j == 3) {
        for (i=0 ; i<256; ++i) {
            x = (2.0/255)*i;
            xx = get_value(x, t_array, 3);
            fprintf(fp, "%f", xx);
            if (i == 255) {
                fprintf(fp, "\n");
            } else {
                fprintf(fp, " ");
            }
        }
    } else if (j == 5) {
        for (i=0 ; i<256; ++i) {
            x = (4.0/255)*i;
            xx = get_value(x, t_array, 5);
            fprintf(fp, "%f", xx);
            if (i == 255) {
                fprintf(fp, "\n");
            } else {
                fprintf(fp, " ");
            }
        }
    } else {
        fclose(fp);
        printf("wrong number of elements: %d instead of 8 or 5 or 3\n",j);
        return -1;
    }
    
    fclose(fp);
    
    return 0;
}
