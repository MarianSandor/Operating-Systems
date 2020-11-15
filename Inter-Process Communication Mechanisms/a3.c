#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>   
#include <sys/sem.h>
#include <unistd.h>     
#include <fcntl.h>      

#define VARIANT 39899
#define PAGE_SIZE 2048
#define SHM_NAME "/KwI81ol"
#define SIZE_SECTION_HEADER 16

unsigned int shm_size = -1;		// shared memory size given in request.
unsigned int file_size = -1;	// size of the mapped file.

int fd_receive, fd_send;		// file descriptors for the two communication pipes.
char *shared_data = NULL;		// pointer to the shared memory area.
char *file_data = NULL;			// pointer to the area of the mapped file.

void unmap_file()	// unmap file if it was mapped before.
{
	if (file_data != NULL)
		munmap(file_data, file_size);

	file_data = NULL;
	file_size = -1;
}

void unmap_shm()	// unmap shared memory if it was mapped before.
{
	if (shared_data != NULL)
		munmap(shared_data, shm_size);

	shared_data = NULL;
	shm_size = -1;
}

int connection() 	// establish connection.
{
	// prepare the message to be sent in case of successful connection.
	char connection_successfully[] = "CONNECT";
	unsigned char size = strlen(connection_successfully) & 0xFF;	

	//defining the names of the pipes.
	char receiving_pipe_name[] = "REQ_PIPE_39899";
	char sending_pipe_name[] = "RESP_PIPE_39899";

	if (mkfifo(sending_pipe_name, 0666) < 0)	// create pipe file.
		return -1;

	if ((fd_receive = open(receiving_pipe_name, O_RDONLY)) < 0)	// opne the receiving pipe.
		return -1;

	if ((fd_send = open(sending_pipe_name, O_WRONLY)) < 0)	// open the sending pipe.
		return -1;

	// announce the successful connection.
	printf("SUCCESS\n");
	write(fd_send, &size, sizeof(unsigned char));
	write(fd_send, connection_successfully, size);

	return 0;
}

void send_error() {		// send the ERROR message through the pipe.
	unsigned char size;
	char error[] = "ERROR";

	size = strlen(error) & 0xFF;
	write(fd_send, &size, sizeof(unsigned char));
	write(fd_send, error, size);
}

void send_success() {		// send the SUCCESS message through the pipe.
	unsigned char size;
	char success[] = "SUCCESS";

	size = strlen(success) & 0xFF;
	write(fd_send, &size, sizeof(unsigned char));
	write(fd_send, success, size);
}

void ping_request()
{
	char pong[] = "PONG";
	unsigned char size = strlen(pong) & 0xFF;
	unsigned int number = (unsigned int) VARIANT;

	// send back PONG string and VARIANT number.
	write(fd_send, &size, sizeof(unsigned char));
	write(fd_send, pong, size);
	write(fd_send, &number, sizeof(number));
}

int create_shm_request()
{
	int shm_fd;

	read(fd_receive, &shm_size, sizeof(shm_size));	// read the size of the shared memory to be created.

	shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0664);	// create the shm and check the creation state.
	if (shm_fd < 0) {
		send_error();
		return -1;
	}

	ftruncate(shm_fd, shm_size);	// allocate the given size.

	shared_data = (char *)mmap(0, shm_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);	// map the shm and check map state.
	if (shared_data == MAP_FAILED) {
		send_error();
		close(shm_fd);
		return -1;
	}

	send_success();

	close(shm_fd);
	return 0;
}

int write_shm_request() 
{
	unsigned char bytes[4];
	unsigned int offset;
	unsigned int value;

	read(fd_receive, &offset, sizeof(offset));	// read offset.
	read(fd_receive, &value, sizeof(value));	// read value.

	if (offset < 0 || offset >= shm_size - 4) {	// check validity of the offset.
		send_error();
		return -1;
	}

	// extract the bytes from the value field.
	bytes[0] = value & 0xFF;
	bytes[1] = (value >> 8) & 0xFF;
	bytes[2] = (value >> 16) & 0xFF;
	bytes[3] = (value >> 24) & 0xFF;

	for (int i = 0; i < 4; ++i) {
		if (bytes[i] < 0 || bytes[i] >= shm_size - 4) {	// check each byte if it is a valid offset.
			send_error();
			return -1;
		}
	}

	for (int i = 0; i < 4; ++i) {
		shared_data[offset + i] = bytes[i];	// write the bytes to the shm area.
	}

	send_success();

	return 0;
}

int map_file_request()
{
	int fd_map_file;
	unsigned char size;
	char *file_name;

	read(fd_receive, &size, sizeof(unsigned char));					// read size of string field.
	file_name = (char *)malloc((((int) size) + 1) * sizeof(char));	

	read(fd_receive, file_name, ((int) size) * sizeof(char));		// read file_name string field.
	file_name[(int) size] = '\0';

	fd_map_file = open(file_name, O_RDONLY);	// open the file for reading and cheking the state.
	if (fd_map_file == -1) {
		send_error();
		free(file_name);
		return -1;
	}

	file_size = (unsigned int) lseek(fd_map_file, 0 ,SEEK_END);	//obtain the file size.
	lseek(fd_map_file, 0, SEEK_SET);

	file_data = (char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd_map_file, 0);	// map the file and check the state of the mapping
	if (file_data == MAP_FAILED) {
		send_error();
		close(fd_map_file);
		free(file_name);
		return -1;
	}

	send_success();

	close(fd_map_file);
	free(file_name);
	return 0;
}

int read_file_offset_request()
{
	unsigned int offset;
	unsigned int no_of_bytes;

	read(fd_receive, &offset, sizeof(offset));				// read offset.
	read(fd_receive, &no_of_bytes, sizeof(no_of_bytes));	// read no_of_bytes.

	// check if there is a file mapped and a shm and the validity of the offset. 
	if (file_data == NULL || shared_data == NULL || offset < 0 || offset >= file_size - no_of_bytes) {
		send_error();
		return -1;
	}

	// write to the shm the required bytes.
	for (unsigned int i = 0; i < no_of_bytes; ++i) {
		shared_data[i] = file_data[offset + i];	
	}

	send_success();

	return 0;
}

// this function has the main purpose of checking if the currently mapped file is a valid SF file.
// based on the parameter section_no value:
// 			-1 --> the function returns 1 if the file has the required format.
//			positive number --> the function return the offset of the section given by the parameter.
// in both cases if the format is violated, the function return -1.
unsigned int check_file(unsigned int section_no)
{
	unsigned char valid_sect_types[] = {62, 12, 34, 91, 48, 89, 43};
	int valid = 0;

    short int header_size;
    unsigned char version;
    unsigned char no_of_sections;
    unsigned int section_index;
    unsigned int sect_offset;
    unsigned char sect_type;

    if (file_data[file_size - 2] != '9' || file_data[file_size - 1] != 'q') {	// checking the magic number.
    	return -1;
    }

    header_size = (file_data[file_size - 4] & 0xFF) | (file_data[file_size - 3] & 0xFF) << 8;	// obtain the header size.
    
    version = (unsigned char) file_data[file_size - header_size];	// get the version number.
    if (version < 115 || version > 195) {							// check the version number.
    	return -1;
    }

    no_of_sections = (unsigned char) file_data[file_size - header_size + 1];	// get the number of sections.
    if (no_of_sections < 9 || no_of_sections > 15) {							// check the number of sections.
    	return -1;
    }

    if ((int) section_no == -1) {	// check if the parameter is -1.
    	return 1;					// it means the function should only check the validity of the file.
    }								// since it reached here, it means the file format is valid and returns 1.

    if (section_no > no_of_sections || section_no < 1) {	// check if the required section_no is between 1 and number of sections.
    	return -1;
    }

    section_index = (file_size - header_size + 2) + (section_no - 1) * SIZE_SECTION_HEADER;	 // compute the index of the required section header.
    section_index += 7;		// increment if by 7 to point to the section type field.

    sect_type = (unsigned char) file_data[section_index];	// read the sect type field.

    for (int i = 0; i < 7; ++i) {				// validate if the required section has a valid sect_type.
    	if (sect_type == valid_sect_types[i]) {
    		valid = 1;
    	}
    }

    if (valid == 0) {
    	return -1;
    }

    section_index++;	// increment the index to pointe to the sect_offfset field.

    // read the section offset.
    sect_offset = (file_data[section_index] & 0xFF) | (file_data[section_index + 1] & 0xFF) << 8 | 
    				(file_data[section_index + 2] & 0xFF) << 16 | (file_data[section_index + 3] & 0xFF) << 24;

    return sect_offset;
}

int read_file_section_request()
{
	unsigned int section_no;
	unsigned int offset;
	unsigned int no_of_bytes;
	unsigned int index;

	read(fd_receive, &section_no, sizeof(section_no));		// read section_no field.
	read(fd_receive, &offset, sizeof(offset));				// read offset field.
	read(fd_receive, &no_of_bytes, sizeof(no_of_bytes));	// read no_of_bytes field.

	index = check_file(section_no);		// initialize index with section offset.
	if ((int) index < 0) {
		send_error();
		return -1;
	}

	index += offset;		// increment the index with the given offset.

	for (int i = 0; i < no_of_bytes; ++i) {
		shared_data[i] = file_data[index + i];		// write to the shm the required bytes.
	}

	send_success();

	return 0;
}

int read_logical_space_request()
{
	short unsigned int header_size;
	unsigned char no_of_sections;
	unsigned int sect_offset;
	unsigned int sect_size;

	unsigned int section_index;		// index in the section header to identifie the fields that describe the section.

	unsigned int logical_index = 0;		// the index in the logical memory space.

	unsigned int physical_offset;		// the offset in the "physical" memory (in the memory area where the file is mapped).

	unsigned int logical_offset;		// the logical offset required.
	unsigned int no_of_bytes;

	read(fd_receive, &logical_offset, sizeof(logical_offset));	// read the logical_offset field.
	read(fd_receive, &no_of_bytes, sizeof(no_of_bytes));		// read the no_of_bytes field.

	if (check_file(-1) != 1) {	// check if the file is a valid SF file. (the parameter is -1 for checking only)
		send_error();
		return -1;
	}

	header_size = (file_data[file_size - 4] & 0xFF) | (file_data[file_size - 3] & 0xFF) << 8;	// get the header size.
	no_of_sections = (unsigned char) file_data[file_size - header_size + 1];	// get the number of sections.

	// iterate through the section headers.
	for (int i = 0; i < (int) no_of_sections; ++i) {
		section_index = (file_size - header_size + 2) + i * SIZE_SECTION_HEADER;	// compute the section index of the current section header.

		// get section offset.
    	section_index += 8;
    	sect_offset = (file_data[section_index ] & 0xFF) | (file_data[section_index + 1] & 0xFF) << 8 | 
    				(file_data[section_index + 2] & 0xFF) << 16 | (file_data[section_index + 3] & 0xFF) << 24;

    	// get section size.
    	section_index += 4;
    	sect_size = (file_data[section_index ] & 0xFF) | (file_data[section_index + 1] & 0xFF) << 8 | 
    				(file_data[section_index + 2] & 0xFF) << 16 | (file_data[section_index + 3] & 0xFF) << 24;

    	// compute the number of neccessary pages to store the current section.
    	int no_of_pages = sect_size / PAGE_SIZE;
    	if (sect_size % PAGE_SIZE != 0) {
    		no_of_pages += 1;
    	}

    	// check if the required logical_offset lays inside the pages of the current section.
    	if (logical_index + no_of_pages * PAGE_SIZE > logical_offset) {
    		// compute the physical_offset being the section offset plus the difference between the required logical offset
    		// 	and the logical index (logical index points to the logical offset of the currecnt section).
			physical_offset = sect_offset + logical_offset - logical_index;;

			for (int j = 0; j < no_of_bytes; ++j) {
				shared_data[j] = file_data[physical_offset + j];	// write to the shm the required bytes.
			}

			break;
		}

		// increment the logical index with the number of bytes used to store the current section.
		logical_index += no_of_pages * PAGE_SIZE;	 
	}

	send_success();

	return 0;
}

int main(int argc, char **argv) 
{
	unsigned char size;		// size of the req_name field (number of bytes).
	char *req_name;			// req_name received through the pipe.

	if (connection() < 0) {		// throws error in case the connection could not be establish.
		printf("ERROR\n");
		printf("cannot create the response pipe | cannot open the request pipe\n");
		exit(1);
	}
	// the first field from the command is always a string field that starts with the size.
	while (read(fd_receive, &size, sizeof(unsigned char))) {			// reading the size of req_name.
		req_name = (char *)malloc((((int) size) + 1) * sizeof(char));	// allocate memory for req_name field.	
		read(fd_receive, req_name, ((int) size) * sizeof(char));		// read req_name.
		req_name[(int) size] = '\0';									// convert it in printable string.

		if (strcmp(req_name, "EXIT") == 0) {			// if command is "EXIT" break the loop and end the program.
			break;
		}

		write(fd_send, &size, sizeof(unsigned char));	// Send back the size field.
		write(fd_send, req_name, size);					// Send back the req_name.

		//identify request and execute it.
		if (strcmp(req_name, "PING") == 0) {	
			ping_request();
		}
		else if (strcmp(req_name, "CREATE_SHM") == 0) {
			unmap_shm();
			create_shm_request();
		}
		else if (strcmp(req_name, "WRITE_TO_SHM") == 0) {
			write_shm_request();
		}
		else if (strcmp(req_name, "MAP_FILE") == 0) {
			unmap_file();
			map_file_request();
		}
		else if (strcmp(req_name, "READ_FROM_FILE_OFFSET") == 0) {
			read_file_offset_request();
		}
		else if (strcmp(req_name, "READ_FROM_FILE_SECTION") == 0) {
			read_file_section_request();
		}
		else if (strcmp(req_name, "READ_FROM_LOGICAL_SPACE_OFFSET") == 0) {
			read_logical_space_request();
		}

		free(req_name);
	}

	unmap_shm();
	unmap_file();

	close(fd_receive);
	close(fd_send);

	return 0;
}

