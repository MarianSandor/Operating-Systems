#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h> 
#include <unistd.h>

int RECURSIVE = 0;
int FILTERING_OPTIONS = 0; // 0 --> No Options; 1 --> name ends with; 2 --> size smaller; 3 --> both
int LIST = 0;
int PARSE = 0;
int EXTRACT = 0;
int FINDALL = 0;

#pragma pack(push, 1)
typedef struct Section_Header {
    char name[7];
    char type;
    int offset;
    int size;
} Section_Header;
#pragma pack(pop)

Section_Header **create_SH(int no) 
{
    Section_Header **sh = malloc(no * sizeof(Section_Header *));

    if (sh) {

        for (int i = 0; i < no; i++) {
            *(sh + i) = (Section_Header *) malloc(sizeof(Section_Header));
        }
    }
    
    return sh;
}

void free_SH(Section_Header **sh, int no) 
{
    if (sh != NULL) {

        for (int i = 0; i < no; i++) {
            if (sh[i] != NULL)
                free(sh[i]);
        }

        free(sh);
    }
}

void print_result(Section_Header **sh, int no_of_sections, int version)
{
    char string_name[7];

    printf("version=%d\n", version);
    printf("nr_sections=%d\n", no_of_sections);

    for (int i = 0; i < no_of_sections; i++) {
        strncpy(string_name, sh[i]->name, 7);
        string_name[7] = '\x00';
        printf("section%d: %s %d %d\n", i+1, string_name,(int) sh[i]->type & 0xff, sh[i]->size);
    }   
}

int validate_sect_type(int sect_type) 
{
    int valid_types[] = {62, 12, 34, 91, 48, 89, 43};

    for (int i = 0; i < 7; i++)
    {
        if (sect_type == valid_types[i])
            return 1;
    }
    
    return 0;
}

int extract_line(int fd, Section_Header *sh, int line) 
{
    char *buf = malloc(sh->size * sizeof(char));
    int curr_line = 1;
    int valid_line = 0;

    lseek(fd, sh->offset, SEEK_SET);
    read(fd, buf, sh->size);

    for (int i = 0; i < sh->size; i++)
    {
        if (curr_line > line)
            break;

        if (((int) (buf[i] & 0xff) == 13) && ((int) (buf[i+1] & 0xff) == 10)) {
            curr_line++;
            i++;
        }
        else if (curr_line == line) {
            if (valid_line == 0) {
                printf("SUCCESS\n");
                valid_line = 1;
            }

            printf("%c", buf[i]);
        }
    }
    
    if (valid_line == 0) {
        printf("ERROR\n");
        printf("invalid line");
    }
    printf("\n");

    free(buf);
    
    return 0;

}

int check_SF(int fd, Section_Header **sh, int no_of_sections)
{
    char *buf = NULL;
    int no_of_lines;

    for (int sect = 0; sect < no_of_sections; sect++) {

        buf = (char *) malloc(sh[sect]->size * sizeof(char));
        no_of_lines = 1;

        lseek(fd, sh[sect]->offset, SEEK_SET);
        read(fd, buf, sh[sect]->size);

        for (int i = 0; i < sh[sect]->size; i++)
        {
            if (no_of_lines > 13)
                break;

            if (((int) (buf[i] & 0xff) == 13) && ((int) (buf[i+1] & 0xff) == 10)) {
                no_of_lines++;
                i++;
            }
        }

        free(buf);

        if (no_of_lines == 13)
            return 1;
    }
    
    return 0;
}

int check_SF_format(char *path, int sect_nr, int line)
{
    int fd;
    char magic[2];
    char version;
    int version_int;
    short int header_size;
    char no_of_sections;
    int no_of_sections_int;
    Section_Header **sh = NULL;

    if ((fd = open(path, O_RDONLY)) < 0) {
        
        return -1; // Could not open file
    }

    //get magic value
    lseek(fd, -sizeof(magic), SEEK_END);
    read(fd, &magic, sizeof(magic));
    lseek(fd, -sizeof(magic), SEEK_CUR);

    if (strcmp(magic, "9q") != 0) {//check magic

        if (PARSE == 1) {
            printf("ERROR\n");
            printf("wrong magic\n");
        }
        if (EXTRACT == 1) {
            printf("ERROR\n");
            printf("invalid file\n");
        }

        close(fd);
        return -1;
    }
    
    //get header_size value
    lseek(fd, -sizeof(header_size), SEEK_CUR);
    read(fd, &header_size, sizeof(header_size));

    //get version value
    lseek(fd, -header_size, SEEK_END);
    read(fd, &version, sizeof(version));
    version_int = (int) version & 0xff;

    if (version_int < 115 || version_int > 195) {//check version

        if (PARSE == 1){
            printf("ERROR\n");
            printf("wrong version\n");
        }
        if (EXTRACT == 1) {
            printf("ERROR\n");
            printf("invalid file\n");
        }

        close(fd);
        return -1;
    }

    read(fd, &no_of_sections, sizeof(no_of_sections));
    no_of_sections_int = (int) no_of_sections & 0xff;

    if (no_of_sections_int < 7 || no_of_sections_int > 13) {//check number of sections

        if (PARSE == 1) {         
            printf("ERROR\n");
            printf("wrong sect_nr\n");
        }
        if (EXTRACT == 1) {
            printf("ERROR\n");
            printf("invalid file\n");
        }

        close(fd);
        return -1;
    }

    sh = create_SH(no_of_sections_int);

    for (int i = 0; i < no_of_sections_int; i++) {
        read(fd, sh[i], sizeof(Section_Header));

        if (validate_sect_type((int) sh[i]->type & 0xff) == 0) {
            
            if (PARSE == 1) {
                printf("ERROR\n");
                printf("wrong sect_types\n");
            }
            if (EXTRACT == 1) { 
                printf("invalid file\n");
                printf("ERROR\n");
            }

            free_SH(sh, no_of_sections);
            close(fd);
            return -1;
        }
    }

    if (PARSE == 1) {
        printf("SUCCESS\n");
        print_result(sh, no_of_sections_int, version_int);
    }

    if (EXTRACT == 1) {
        if (sect_nr > no_of_sections_int || sect_nr < 1) {
            printf("ERROR\n");
            printf("invalid section\n");

            free_SH(sh, no_of_sections);
            close(fd);
            return -1;
        }

        extract_line(fd, sh[sect_nr - 1], line);
    }

    if (FINDALL == 1) {
        int result = check_SF(fd, sh, no_of_sections_int);

        free_SH(sh, no_of_sections);
        close(fd);

        return result;
    }

    free_SH(sh, no_of_sections);

    close(fd);

    return 0;
}

int check_string_ends_with(char *a, char *b) 
{
    if (strcmp(a +(strlen(a) - strlen(b)), b) == 0) {
        return 0;
    }

    return -1;
}

int check_file_size_less(char *file, int size) 
{
    struct stat st;
    stat(file, &st);

    if (st.st_size >= size) {
        return -1;
    }

    return 0;
}

int list(DIR *d, char *path, char* ends, int size)
{
    struct dirent *dir = NULL;
    char *new_path = NULL;

    while ((dir = readdir(d)) != NULL)  {

        if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0 && dir->d_name[0] != '.') {

            new_path =(char *) malloc(strlen(path) + strlen(dir->d_name) + 2);
            sprintf(new_path, "%s/%s", path, dir->d_name);
            
            if (FILTERING_OPTIONS == 1 || FILTERING_OPTIONS == 3) {

                if (check_string_ends_with(dir->d_name, ends) == 0)
                    printf("%s\n", new_path);
            }
            else if (FILTERING_OPTIONS == 2 || FILTERING_OPTIONS == 3) {

                if (dir->d_type != DT_DIR)
                    if (check_file_size_less(new_path, size) == 0)
                        printf("%s\n", new_path);
            }
            else if (FINDALL == 1) {
                
                if (dir->d_type != DT_DIR)
                    if (check_SF_format(new_path, 0, 0) == 1) 
                        printf("%s\n", new_path);
            }
            else
                printf("%s\n", new_path);

            if (RECURSIVE == 1 && dir->d_type == DT_DIR) {  

                DIR *new_d = opendir(new_path);

                if (new_d != NULL)
                    list(new_d, new_path, ends, size);
            }


            free(new_path);
            new_path = NULL;
        }
    }

    closedir(d);

    return 0;
}

int list_files(char *path, char *ends, int size)
{
    DIR *d = opendir(path);

    if (d == NULL) {
        printf("ERROR\n");
        printf("invalid directory path\n");
        return -1;
    }

    printf("SUCCESS\n");

    list(d, path, ends, size);   

    return 0;
}

int main(int argc, char **argv)
{
    char *path = NULL;
    char *ends = NULL;
    int size = __INT_MAX__;
    int sect_nr = -1;
    int line = -1;

    for (int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "variant") == 0) {
            printf("39899\n");
        }
        else if (strstr(argv[i], "path=") != NULL) {
            path = malloc(strlen(argv[i]) - 4);
            strcpy(path, argv[i] + 5);
        } 
        else if (strstr(argv[i], "name_ends_with=") != NULL) {
            ends = malloc(strlen(argv[i]) - 14);
            strcpy(ends, argv[i] + 15);
            
            if (FILTERING_OPTIONS == 0)
                FILTERING_OPTIONS = 1;
            else if (FILTERING_OPTIONS == 2)
                FILTERING_OPTIONS = 3;
        }
        else if (strstr(argv[i], "size_smaller=") != NULL) {
            sscanf(argv[i], "size_smaller=%d", &size);

            if (FILTERING_OPTIONS == 0)
                FILTERING_OPTIONS = 2;
            else if (FILTERING_OPTIONS == 1)
                FILTERING_OPTIONS = 3;
        }
        else if (strcmp(argv[i], "recursive") == 0) {
            RECURSIVE = 1;
        }
        else if (strcmp(argv[i], "list") == 0) {
            LIST = 1;
        }   
        else if (strcmp(argv[i], "parse") == 0) {
            PARSE = 1;
        }     
        else if (strcmp(argv[i], "extract") == 0) {
            EXTRACT = 1;
        }
        else if (strstr(argv[i], "section=") != NULL) {
            sscanf(argv[i], "section=%d", &sect_nr);
        }
        else if (strstr(argv[i], "line=") != NULL) {
            sscanf(argv[i], "line=%d", &line);
        }
        else if (strcmp(argv[i], "findall") == 0) {
            FINDALL = 1;
            RECURSIVE = 1;
        }
    } 

    if (LIST == 1 || FINDALL == 1)
        list_files(path, ends, size);

    if (PARSE == 1 || EXTRACT == 1)
        check_SF_format(path, sect_nr, line);

    if (ends != NULL)
        free(ends);
    
    free(path);

    return 0;
}