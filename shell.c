//shell.c
//Dongpeng Xia
//This program is a shell that supports ;, &, <, >, and | characters.
//	proc1 | proc2 | proc3
//	proc1 < tmp.txt
//	proc1 > tmp.txt
//	proc1 & proc2
//	proc1 ; proc2
//Built in commands include cd, exit, and history.
//	cd /Users/Documents/
//	exit
//	history
//	history	-c

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE_LENGTH 2048	//max input line length is 2048 characters
#define HISTORY_LINES 100	//shell history has max capacity of 100 lines

//store history as a circular array of size HISTORY_LINES
struct history {
	char** list;
	int lines_filled;
	int start_index;
};

//history functions
void history_add(char* next_line, struct history* hist);
char* history_at(struct history* hist, int index);
void history_end(struct history* hist);
void history_print(struct history* hist);
void history_print_at(struct history* hist, int index);
void history_start(struct history* hist);

//takes command and sees if it is a cd command
//if so changes directory and returns true
//otherwise returns false
bool change_directory(char* command);

//remove all instances of ch from start and end of line,
//returns edited copy of line without any consecutive ch
char* clean_line(char* line, char ch);

//closes pipes for one command
void close_pipes(int* all_the_pipes, int num_pipes_needed);

//return number of times search appears in str
int count_num_in(char* str, char search);

//creates pipes based on number needed for one command
int* create_pipes(int num_pipes_needed);

//method to execute a process
//Parameters: program name, arguments, input pipe, output pipe,
//redirected file input, redirected file output, and background y/n
int exec_process (char* process_name, char** arguments, int new_in, int new_out,
			char* input_file, char* output_file, bool background);

//adds spacing around special characters like &, <, >, and |
char* fence_special_chars(char* line);

//split a sanitized line by semicolons
char** get_commands(char* cleaned_line);

//get next line from stdin
char* get_next_line(void);

//process one command (originally semicolon demarcated)
void handle_command(char* command, struct history* shell_history);

//check if command is an exit, if so then exit the program
void handle_exits(char* command, struct history* shell_history);

//returns true if parsedCommands is a history command, false otherwise
//executes corresponding history function
//history -c := clear history
//history # := execute command number offset# in history
//history := print history
bool handle_history(char** parsed_commands, struct history* shell_history);

//handle non-built-in commands
void handle_non_built_ins(char* temp, char** words, int* all_the_pipes);

//returns true if str is a non-negative number, false otherwise
bool is_num(char* str);

//handles a new line from get_next_line()
void process_line(char* next_line, struct history* shell_history);

//split a command by spaces
char** split_command(char* line);

//main
int main(int argc, const char * argv[])
{
	//start history as empty
	struct history shell_history;
	history_start(&shell_history);
	
	while(true) {
		printf("$");
		char* next_line = get_next_line();
		process_line(next_line, &shell_history);
	}
	return 0;
}

//takes command and sees if it is a cd command
//if so changes directory and returns true
//otherwise returns false
bool change_directory(char* command)
{
	bool cd = false;
	
	if(strncmp(command, "cd ", 3) == 0) {
		//take out trailing and leading spaces
		//from destination directory
		char* temp = &((command)[(strlen(command)-1)]);
		while(*temp == ' ')
			temp--;
		temp++;
		*temp = '\0';
		temp = command;
		temp++;
		temp++;
		while(*temp == ' ')
			temp++;
		
		//change directory
		if(chdir(temp) == -1)
			fprintf(stderr, "Error: %s\n", strerror(errno));
		cd = true;
	}
	return cd;
}

//remove all instances of ch from front and end of line
//remove all double instances of ch in line
char* clean_line(char* line, char ch)
{
	char* new_line;
	if ((new_line = malloc(strlen(line) + 1)) == NULL) //+1 for \0
		fprintf(stderr, "Error: %s\n", strerror(errno));
	char* tmp = new_line;
	char* line_iterator = line;
	
	//skip past starting ch's
	while(*line_iterator == ch)
		line_iterator++;
	
	//copy string except replace any consecutive ch with a single ch
	bool is_first_inst = true;
	while(*line_iterator != '\0') {
		if(*line_iterator != ch) {
			*tmp = *line_iterator;
			tmp++;
			is_first_inst = true;
		}
		else if(is_first_inst) {
			*tmp = ch;
			tmp++;
			is_first_inst = false;
		}
		line_iterator++;
	}
	
	//get rid of last ch if there is one
	tmp--;
	if(*tmp != ch)
		tmp++;
	*tmp = '\0';
	
	return new_line;
}

//closes pipes for one command
void close_pipes(int* all_the_pipes, int num_pipes_needed)
{
	//close all pipes when finished
	for(int p = 0; p < num_pipes_needed * 2; p += 2) {
		if(close(all_the_pipes[p]) == -1)
			fprintf(stderr, "Error: %s\n", strerror(errno));
	}
	
	free(all_the_pipes);
}

//return number of times search appears in str
int count_num_in(char* str, char search)
{
	int num = 0;
	int index = 0;
	while(str[index] != '\0') {
		if(str[index] == search)
			num++;
		index++;
	}
	return num;
}

//creates pipes based on number needed for one command
int* create_pipes(int num_pipes_needed)
{
	int* all_the_pipes;
	if((all_the_pipes = malloc(2 * num_pipes_needed * sizeof(int))) == NULL)
		fprintf(stderr, "Error: %s\n", strerror(errno));
	for(int pipe_num = 0; pipe_num < num_pipes_needed; pipe_num++) {
		if((pipe(&all_the_pipes[pipe_num*2])) == -1)
			fprintf(stderr, "Error: %s\n", strerror(errno));
	}
	return all_the_pipes;
}

//method to execute a process
//Parameters: program name, arguments, input pipe, output pipe,
//redirected file input, redirected file output, and background y/n
//returns 0 on success, -1 on failure
int exec_process (char* process_name, char** arguments, int new_in, int new_out,
		  char* input_file, char* output_file, bool background)
{
	int ret = 0;
	int parent_or_child = fork();
	if (parent_or_child < 0) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
	} else if (parent_or_child == 0) { //returns PID of child to parent
		//child case
		if(new_in != -1) {
			//input coming from pipe
			if(dup2(new_in, STDIN_FILENO) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
			if(close(new_in) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		} else if(input_file != NULL) {
			//redirect input to file
			if(freopen(input_file, "r", stdin) == NULL)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		}
		
		if(new_out != -1) {
			//output going to pipe
			if(dup2(new_out, STDOUT_FILENO) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
			if(close(new_out) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		} else if(output_file != NULL) {
			//redirect output to file
			if(freopen(output_file, "w", stdout) == NULL)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		}
		
		if(execv(process_name, arguments) == -1)
			fprintf(stderr, "Error: %s\n", strerror(errno));
		
		ret = -1; //return value if child failed to exec
	} else {
		//parent
		if(!background) {
			//wait for child if child is not a background process
			if(waitpid(parent_or_child, NULL, 0) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		}
		if(new_out != -1) {
			//let child finish writing first
			if(close(new_out) == -1)
				fprintf(stderr, "Error: %s\n", strerror(errno));
		}
	}
	return ret;
}

//add spaces to left and right of special characters (|, & , <, >)
char* fence_special_chars(char* line)
{
	char* new_line;
	//worst case: entire line is special chars
	if((new_line = malloc((strlen(line)+1)*2)) == NULL)
		fprintf(stderr, "Error: %s\n", strerror(errno));
	char* tmp = new_line;
	*tmp = ' ';
	new_line++;
	char* line_iterator = line;
	while(*line_iterator != '\0') {
		if(*line_iterator == '&') {
			if((*tmp != '&') && (*tmp != ' ')) {
				tmp++;
				*tmp = ' ';
			}
			tmp++;
			*tmp = *line_iterator;
			if(*(tmp-1) == '&' ||
			(*(line_iterator+1)!=' ' && *(line_iterator+1)!='&')) {
				tmp++;
				*tmp = ' ';
			}
		} else if(*line_iterator == '|' ||
			  *line_iterator == '<' || *line_iterator == '>' ||
			  *line_iterator == ';') {
			if(*tmp != ' ') {
				tmp++;
				*tmp = ' ';
			}
			tmp++;
			*tmp = *line_iterator;
			tmp++;
			*tmp = ' ';
		} else {
			if(*line_iterator != ' ' ||
			   (*line_iterator == ' ' && *tmp != ' ')) {
				tmp++;
				*tmp = *line_iterator;
			}
		}
		line_iterator++;
	}
	tmp++;
	*tmp = '\0';
	
	return new_line;
}

//split a sanitized line by semicolons
char** get_commands(char* cleaned_line)
{
	char** commands;
	
	//+1 for last command without ;, +1 for NULL ending
	if((commands = malloc((count_num_in(cleaned_line,';')+2)*sizeof(char*)))
	   == NULL) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
	}
	char* line_copy;
	if((line_copy = strdup(cleaned_line)) == NULL)
		fprintf(stderr, "Error: %s\n", strerror(errno));
	char* cmd;
	
	int cmd_index = 0;
	while((cmd = strsep(&line_copy, ";")) != NULL) {
		//remove start and end space
		if(cmd[0] == ' ')
			cmd++;
		if(cmd[strlen(cmd) - 1] == ' ')
			cmd[strlen(cmd) - 1] = '\0';
		
		//do not add empty commands to list
		if(strlen(cmd) > 1 || (strlen(cmd) == 1 && !isspace(cmd[0]))) {
			commands[cmd_index] = cmd;
			cmd_index++;
		}
	}
	
	commands[cmd_index] = NULL; //specify ending of list
	return commands;
}

//get next line from stdin
char* get_next_line(void)
{
	char* next_line;
	if((next_line = malloc(MAX_LINE_LENGTH * sizeof(char))) == NULL) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		next_line = NULL;
	} else {
		if(fgets(next_line, MAX_LINE_LENGTH, stdin) == NULL) {
			//error with fgets
			fprintf(stderr, "Error: %s\n", strerror(errno));
			next_line = NULL;
		}
	}
	return next_line;
}

//process one command (originally semicolon demarcated)
void handle_command(char* command, struct history* shell_history)
{
	handle_exits(command, shell_history);
	
	if(!change_directory(command)) {
		//clean command input so special characters (|, &, <, >)
		//have spaces to left and right
		char* temp = fence_special_chars(command);
		
		//split command by spaces
		char** parsed_commands = split_command(temp);
		
		if(!handle_history(parsed_commands, shell_history)) {
			//create pipes
			int num_pipes_needed = count_num_in(temp, '|');
			int* all_the_pipes = create_pipes(num_pipes_needed);
			
			handle_non_built_ins(temp, parsed_commands,
					     all_the_pipes);
			
			close_pipes(all_the_pipes, num_pipes_needed);
		}
		free(parsed_commands);
	}
}

//check if command is an exit, if so then exit the program
void handle_exits(char* command, struct history* shell_history)
{
	//command = 'exit\0' or command = 'exit *',
	if(strncmp(command, "exit", 5) == 0 ||
	   strncmp(command, "exit ", 5) == 0) {
		history_end(shell_history);
		exit(0);
	}
}

//returns true if parsedCommands is a history command, false otherwise
//executes corresponding history function
//history -c := clear history
//history # := execute command number # in history
//history := print history
bool handle_history(char** parsed_commands, struct history* shell_history)
{
	bool history = false;
	
	if(parsed_commands[0] != NULL &&
	   strcmp(parsed_commands[0], "history") == 0) {
		if(parsed_commands[1] == NULL)
			history_print(shell_history);
		else if(strcmp(parsed_commands[1], "-c") == 0) {
			history_end(shell_history);
			history_start(shell_history);
		} else if(is_num(parsed_commands[1])) {
			char* past = history_at(shell_history,
						atoi(parsed_commands[1]));
			if(past != NULL)
				process_line(past, shell_history);
		}
		history = true;
	}
	return history;
}

//handle non-built-in commands
void handle_non_built_ins(char* temp, char** words,int* all_the_pipes)
{
	//variables for process execution circumstances
	char* program = NULL; //name of program
	//arguments list must be NULL terminated
	char** arguments = malloc(MAX_LINE_LENGTH * sizeof(char*));
	int argument_num = 0;	//# of arguments
	int curr_pipe_in = -1;	//input pipe for current program
	int next_pipe_in = -1;	//input pipe for next program
	int pipe_out = -1;	//output pipe
	char* input_file = NULL;//redirect input from file
	char* output_file = NULL;//redirect output to file
	int pipe_num = 0;	//current pipe number
	bool background = false;//true if run in background, false otherwise
	bool exec_now = false;	//true if program is ready for execution
	
	//process command by words (space separated)
	int pc_index = 0;
	while(words[pc_index] != NULL) {
		//if new program for execution, set name
		if(program == NULL)
			program = words[pc_index];
		
		//add to arguments list
		arguments[argument_num] = words[pc_index];
		argument_num++;
		arguments[argument_num] = NULL; //list must be NULL terminated
		
		//look ahead one word to see what will be processed next
		//(useful for processing special characters like <, >, &, |)
		if(words[pc_index + 1] != NULL) {
			if(strcmp(words[pc_index + 1], "<" ) == 0) {
				exec_now = true;
				//redirect stdin to file
				if(words[pc_index + 2] != NULL) {
					input_file = words[pc_index + 2];
					pc_index++;
				}
				pc_index++;
			}
			if((words[pc_index + 1] != NULL) &&
			   strcmp(words[pc_index + 1], ">") == 0) {
				exec_now = true;
				//redirect stdout to file
				if(words[pc_index + 2] != NULL) {
					output_file = words[pc_index + 2];
					pc_index++;
				}
				pc_index++;
				
				//case to support (process > file1 < file2)
				if((words[pc_index + 1] != NULL) &&
				   strcmp(words[pc_index + 1], "<" ) == 0) {
					exec_now = true;
					//redirect stdin to file
					if(words[pc_index + 2] != NULL) {
						input_file = words[pc_index+2];
						pc_index++;
					}
					pc_index++;
				}
			}
			if((words[pc_index + 1] != NULL) &&
			   strcmp(words[pc_index + 1], "&") == 0) {
				exec_now = true;
				//run in background
				background = true;
				pc_index++;
			}
			if((words[pc_index + 1] != NULL) &&
			   strcmp(words[pc_index + 1], "|") == 0) {
				exec_now = true;
				//peek at next program
				if(words[pc_index + 2] != NULL) {
					//set pipe out
					pipe_out = all_the_pipes[pipe_num*2+1];
					//set pipe in of next process
					next_pipe_in =all_the_pipes[pipe_num*2];
					//run output pipe in background
					background = true;
					//used a pipe
					pipe_num++;
				}
				pc_index++;
			}
		}
		if(exec_now) {
			if(exec_process(program, arguments, curr_pipe_in,
					pipe_out, input_file, output_file,
					background) == -1) {
				//return of -1 means child failed
				exit(1); //this only exits the child copy
			}
			//prepare process values for next process
			program = input_file = output_file = NULL;
			background = exec_now = false;
			curr_pipe_in = next_pipe_in;
			argument_num = 0;
			next_pipe_in = -1;
			pipe_out = -1;
		}
		pc_index++; //move on to next word in command
	}
	
	//run final command (no special characters at end)
	if(program != NULL) {
		if(exec_process(program, arguments, curr_pipe_in, pipe_out,
				input_file, output_file, background) == -1) {
			//return of -1 means child did not exec correctly
			exit(1); //this only exits the child copy
		}
	}
	free(arguments);
}

//returns true if str is a non-negative number, false otherwise
bool is_num(char* str)
{
	bool still_a_number = true;
	char* tmp = str;
	while(still_a_number && *tmp != '\0') {
		if(!isdigit(*tmp))
			still_a_number = false;
		tmp++;
	}
	return still_a_number;
}

//handles a new line from get_next_line()
void process_line(char* next_line, struct history* shell_history)
{
	history_add(next_line, shell_history);
	
	char* clean_new_line = clean_line(next_line, '\n');
	char* clean_tab = clean_line(clean_new_line, '\t');
	char* clean_spaces = clean_line(clean_tab, ' ');
	char* clean_semicolons = clean_line(clean_spaces, ';');
	char* clean_redirect_in = clean_line(clean_semicolons, '<');
	char* clean_redirect_out = clean_line(clean_redirect_in, '>');
	char* cleaned_line = clean_line(clean_redirect_out, '|');
	
	//split line by semicolons
	char** commands = get_commands(cleaned_line);
	
	//go one command at a time (semicolon separated)
	int cmd_index = 0;
	while(commands[cmd_index] != NULL) {
		handle_command(commands[cmd_index], shell_history);
		cmd_index++;
	}
	
	free(commands);
	free(cleaned_line);
	free(clean_redirect_out);
	free(clean_redirect_in);
	free(clean_semicolons);
	free(clean_spaces);
	free(clean_tab);
	free(clean_new_line);
}

//split a command by spaces
char** split_command(char* line)
{
	char** words;
	char* copy;
	char* word;
	//+1 for first word, +1 for NULL ending
	if(((words = malloc((count_num_in(line,' ')+2)*sizeof(char*))) == NULL))
		fprintf(stderr, "Error: %s\n", strerror(errno));
	if((copy = strdup(line)) == NULL)
		fprintf(stderr, "Error: %s\n", strerror(errno));
	int wd_index = 0;
	while((word = strsep(&copy, " ")) != NULL) {
		//do not add empty words to list
		if(strlen(word) > 1 || (strlen(word)==1 && !isspace(word[0]))) {
			words[wd_index] = word;
			wd_index++;
		}
	}

	words[wd_index] = NULL; //specify ending of list
	return words;
}

//------------------------------------------------------------------------------
//start of history functions
//------------------------------------------------------------------------------

void history_add(char* next_line, struct history* hist)
{
	//do not add to history if line is just a [ENTER]
	if(!(strlen(next_line) == 1 && next_line[0] == '\n')) {
		if(hist->lines_filled == HISTORY_LINES) {
			//replace oldest entry
			free(hist->list[hist->start_index]);
			if((hist->list[hist->start_index] = strdup(next_line))
			   == NULL)
				fprintf(stderr, "Error: %s\n", strerror(errno));
			hist->start_index = (hist->start_index+1)%HISTORY_LINES;
		} else {
			if((hist->list[(hist->start_index + hist->lines_filled)
				% HISTORY_LINES] = strdup(next_line)) == NULL)
				fprintf(stderr, "Error: %s\n", strerror(errno));
			hist->lines_filled++;
		}
	}
}

char* history_at(struct history* hist, int index)
{
	char* ptr;
	if(index < 0 || hist->lines_filled <= index) {
		//index out of bounds error
		fprintf(stderr, "Error: Invalid offset\n");
		ptr =  NULL;
	} else
		ptr = hist->list[(hist->start_index + index) % HISTORY_LINES];
	return ptr;
}

void history_end(struct history* hist)
{
	int original_size = hist->lines_filled;
	for(int index = original_size - 1; index >= 0; index--) {
		free(history_at(hist, index));
		hist->lines_filled--;
	}
	hist->start_index = 0;
}

void history_print(struct history* hist)
{
	for(int index = 0; index < hist->lines_filled; index++)
		history_print_at(hist, index);
}

void history_print_at(struct history* hist, int index)
{
	printf("\t%d\t%s", index, history_at(hist, index));
}

void history_start(struct history* hist)
{
	hist->lines_filled = 0;
	hist->start_index = 0;
	
	//allocate history array
	if((hist->list = malloc(HISTORY_LINES * sizeof(char*))) == NULL)
		fprintf(stderr, "Error: %s\n", strerror(errno));
}

//------------------------------------------------------------------------------
//end of history functions
//------------------------------------------------------------------------------