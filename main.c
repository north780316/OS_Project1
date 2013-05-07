/* OS Project1 -- R00942088 NTU-GICE. */
#define _POSIX_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#define MAX_PLAYER_NUM 3
#define SYS_THREAD_RAND 251
#define OP_SEED 1
#define OP_RAND 2
#define SNAKE_ICON "@"
#define FOOD_ICON "X"

typedef enum { DIRECTION_UP, DIRECTION_DOWN, DIRECTION_LEFT, DIRECTION_RIGHT } DIRECTION;

typedef struct 
{ 
	int x, y; 
} pos_T;

typedef struct 
{ 
	pos_T *pos, *backPos;
	int head, tail, size, BUFSIZE;
} queue_T;
queue_T snakeQueue[MAX_PLAYER_NUM];

typedef struct 
{
	int M, N, originxTerm, originyTerm, playerNum;
	double scoreStep[MAX_PLAYER_NUM], score[MAX_PLAYER_NUM];
	WINDOW *grid[MAX_PLAYER_NUM], *map[MAX_PLAYER_NUM];

	pthread_t tid_snake[MAX_PLAYER_NUM];
	int message_pipe[MAX_PLAYER_NUM][2],
		timeout[MAX_PLAYER_NUM];
	bool is_die[MAX_PLAYER_NUM], usermode;
	pos_T food_pos[MAX_PLAYER_NUM];
	FILE *debug_report[MAX_PLAYER_NUM];
	DIRECTION previous_direction[MAX_PLAYER_NUM];
} mparm_T;
mparm_T params;

pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
int usermode_seeds[MAX_PLAYER_NUM];

typedef void sig_func ( int );
static void error_check ( bool, const char * );
static void handle_cmdline_args ( int , char *[] );
static void start_cursor_mode ();
static void end_cursor_mode ();
static void draw_frame ();
static void generate_snakes ();
static void run_loop ();
static void init_queue ( queue_T * );
static void init_snake ( int );
static void enqueue ( queue_T *, pos_T * );
static void dequeue ( queue_T *, pos_T * );
static void *tf_snake ( void * );
static void tf_handler1 ( void * );
static void move_snake ( int, DIRECTION );
static void __move_snake ( int, pos_T * );
static void __eat_food ( int, pos_T * );
static void gameover ( int );
static void sig_usr1 ( int );
static void generate_food ( int );
static void add_plugin ( int, int );
static void set_seeds ();
static bool player_i_own_this_key ( int, int );
static bool is_hit_grid ( int, pos_T * );
static bool is_plugin_key ( int, int );
static int get_thread_safe_rand ( int, int );
static int usermode_thread_rand ( int, int );
static pos_T get_next_pos ( pos_T *, DIRECTION );
static DIRECTION get_opposite_direction ( int );
static DIRECTION get_next_direction ( int, int );
static sig_func *set_signal_handler ( int, sig_func * );

int
main ( int argc, char *argv[] ) 
{
	// Use SIGUSR as a 'Game Over' signal.
	set_signal_handler( SIGUSR1, sig_usr1 );

	// Handle command line argument via GNU 'getopt' library.
	handle_cmdline_args( argc, argv );

	// Initail thread base random number generator.
	set_seeds();

	// Use 'Ncurses library' to control terminal.
	start_cursor_mode();
	draw_frame();

	// Use 'POSIX thread' library to manipulate individual snakes.
	generate_snakes();

	// Enter main loop -- run game !
	run_loop ();
	
	return 0;
}

sig_func *
set_signal_handler ( int signo, sig_func *func )
{
	struct sigaction act, oact;
	sigemptyset( &act.sa_mask );
	act.sa_handler = func;
	sigaction( signo, &act, &oact );
	return oact.sa_handler;
}

void
sig_usr1 ( int signo )
{
	end_cursor_mode();
	printf( "Game Over !\n" );
	printf( "\n-------------\n" );
	int win_player = 0;
	for ( int i = 0; i < params.playerNum; ++i )
	{
		printf( "Player %d get score %.1f.\n", i, params.score[i] );
		if ( params.score[i] > params.score[win_player] )	
			win_player = i;
	}
	printf( "\n-------------\n" );
	printf( "Player %d is winner ^_^ !\n", win_player );
	printf( "The highest score is %.1f.\n", params.score[win_player] );
	exit(0);
}

void 
error_check ( bool error_occur, const char *str )
{
	if ( error_occur )
	{
		end_cursor_mode();
		fprintf( stderr, "snake program error: %s", str );
		exit(1);
	}
}

void 
handle_cmdline_args ( int argc, char *argv[] )
{
	error_check( argc < 3, "snake usage: ./snake M N [-player n] [-usermode]\n" );
	memset( &params, 0, sizeof(params) );
	params.M = atoi(argv[1]);
	params.N = atoi(argv[2]);
	if ( params.N < 4 )
	{
		fprintf( stderr, "N should larger 4.\n" );	
		exit(1);
	}
	params.playerNum = 2; // Default player number.

	// Open debug report.
	opterr = 0;
	struct option long_options[] = 
	{
		{"player", required_argument, NULL, 1},
		{"usermode", no_argument, NULL, 2},
		{0, 0, 0, 0}
	};
	int ch;
	while ( (ch = getopt_long_only(argc, argv, "", long_options, NULL)) != EOF )
	{
		switch ( ch )	
		{
			case 1 :
				params.playerNum = atoi(optarg);
				break;

			case 2 :
				params.usermode = true;
				break;

			default :
				break;
		}
	}
/*	
	for ( int i = 0; i < params.playerNum; i++ )
	{
		char name[BUFSIZ];
		sprintf( name, "debug_report_id%d", i );
		params.debug_report[i] = fopen( name, "w" );
		setbuf( params.debug_report[i], 0 );
	}
*/
}

void
start_cursor_mode ()
{
	// Ensure calling 'endwin()'.
	initscr();
	noecho();
	cbreak();
    intrflush( stdscr, false );
	keypad( stdscr, true );
	start_color();
	refresh();
	curs_set(0);

	atexit( end_cursor_mode ); 
}

void
end_cursor_mode ()
{
	pthread_mutex_lock( &display_lock );
	if ( !isendwin() )
		endwin();
	pthread_mutex_unlock( &display_lock );
}

void
draw_frame ()
{
	params.originxTerm = 0;
	params.originyTerm = 0;

	// Draw grid of each player.
	for ( int i = 0; i < params.playerNum; ++i )
	{
		int originx = params.originxTerm + (params.N + 3) * i,
			originy = params.originyTerm;
		params.grid[i] = newwin( params.M + 2, params.N + 2, originy, originx );
		error_check( params.grid[i] == NULL, "ncurses draw grid 1 fail.\n" );
		error_check( box( params.grid[i], ACS_VLINE, ACS_HLINE ) != OK, "ncurses draw box error.\n" );
		params.map[i] = subwin( params.grid[i], params.M, params.N, originy + 1, originx + 1 );
		error_check( params.map[i] == NULL, "ncurses draw map fail.\n" );
		mvwprintw( stdscr, params.originyTerm + params.M + 2, params.originxTerm + (params.N + 3) * i, "* Player %d", i );
		mvwprintw( stdscr, params.originyTerm + params.M + 3, params.originxTerm + (params.N + 3) * i, "* Score  %.1f", params.score[i] );
		refresh();
		wrefresh( params.grid[i] );
		wrefresh( params.map[i] );
	}
}

void
init_queue ( queue_T *Q )
{
	memset( Q, 0, sizeof(queue_T) );
	Q->BUFSIZE = params.M * params.N; // This size can cover full map.
	Q->pos = (pos_T *) calloc ( Q->BUFSIZE, sizeof(pos_T) );
}

void
init_snake ( int id )
{
	// Push initial position into snakeQueue[id].
	pos_T initPos = { .x = params.N/2-2, .y = params.M/2 };
	for ( int i = 0; i < 3; ++i )
	{
		enqueue( &snakeQueue[id], &initPos );
		pthread_mutex_lock( &display_lock );
		mvwprintw( params.map[id], initPos.y, initPos.x, SNAKE_ICON );
		wrefresh( params.map[id] );
		pthread_mutex_unlock( &display_lock );
		initPos.x += 1;
	}
	params.previous_direction[id] = DIRECTION_LEFT;
}

void
enqueue ( queue_T *Q, pos_T *obj )
{
	error_check( Q->pos == NULL, "use uninitialze queue.\n" );
	error_check( Q->size == Q->BUFSIZE, "queue buffer is overflow.\n" );
	Q->pos[Q->tail] = *obj;
	Q->backPos = &Q->pos[Q->tail];
	Q->tail = (Q->tail + 1) % Q->BUFSIZE;
	Q->size++;
}

void
dequeue ( queue_T *Q, pos_T *obj )
{
	error_check( Q->pos == NULL, "use uninitialze queue.\n" );
	error_check( Q->size == 0, "pop empty queue.\n" );
	*obj = Q->pos[Q->head];
	Q->head = (Q->head + 1) % Q->BUFSIZE;
	Q->size--;
}

void
generate_snakes ()
{
	for ( int i = 0; i < params.playerNum; ++i )
	{
		// Initialize each requirments of snake.
		init_queue ( &snakeQueue[i] );
		init_snake(i);
		generate_food(i);
		error_check( pipe( params.message_pipe[i] ) == -1, "open pipe error.\n" );
		params.timeout[i] = 250000;
		params.scoreStep[i] = 100;
		pthread_create( &params.tid_snake[i], NULL, tf_snake, (void *)i );
	}
}

void
run_loop ()
{
	while ( true )
	{
		// Wait stdandard input in 'block' scheme.
		int key = getch();

		// Check this key should pass to what player.
		for ( int i = 0; i < params.playerNum; i++ )
		{
			if ( player_i_own_this_key(i, key) )
				error_check( write(params.message_pipe[i][1], &key, sizeof(int)) == -1, "write pipe error.\n" );
		}
	}
}

void 
tf_handler1 ( void *args )
{
	pthread_mutex_unlock( &display_lock );
}

void *
tf_snake ( void *args )
{
	pthread_cleanup_push( tf_handler1, NULL );

	int id = (int) args;

	DIRECTION direction = DIRECTION_RIGHT;
	do {
		direction = get_thread_safe_rand( OP_RAND, id + 1 ) % 4; // Default direction.
	} while ( direction == params.previous_direction[id] );

	while ( true )
	{
		struct timeval tmv;
		tmv.tv_sec = 0;
		tmv.tv_usec = params.timeout[id];
		fd_set listen_fd;
		FD_ZERO( &listen_fd );
		FD_SET( params.message_pipe[id][0], &listen_fd );
		if ( select(params.message_pipe[id][0] + 1, &listen_fd, NULL, NULL, &tmv) > 0 )
		{
			int keybuf;
			error_check( read( params.message_pipe[id][0], &keybuf, sizeof(int) ) == -1, "read error.\n" );
			if ( is_plugin_key(id, keybuf) )
				add_plugin(id, keybuf);
			else if ( get_next_direction(direction, keybuf) != params.previous_direction[id] )
				direction = get_next_direction( id, keybuf );
			usleep(tmv.tv_sec*1000000 + tmv.tv_usec);
		}
		// Move to next position.
		move_snake( id, direction );
	}

	pthread_cleanup_pop(0);
	return (void *) 0;
}

void
move_snake ( int id, DIRECTION direction )
{
	pos_T next_pos = get_next_pos( snakeQueue[id].backPos, direction );
	if ( is_hit_grid(id, &next_pos) )
		gameover(id);

	params.previous_direction[id] = get_opposite_direction( direction );
	if ( next_pos.x == params.food_pos[id].x && next_pos.y == params.food_pos[id].y )
		__eat_food( id, &next_pos );
	else
		__move_snake( id, &next_pos );
}

void
__eat_food ( int id, pos_T *next_pos )
{
	pthread_mutex_lock( &display_lock );

	// Push pos into snake and plot this new point.
	mvwprintw( params.map[id], next_pos->y, next_pos->x, SNAKE_ICON ); 
	enqueue( &snakeQueue[id], next_pos );
	params.score[id] += params.scoreStep[id];
	mvwprintw( stdscr, params.originyTerm + params.M + 3, params.originxTerm + (params.N + 3) * id, "* Score  %.1f", params.score[id] );
	refresh();
	wrefresh( params.map[id] );

	pthread_mutex_unlock( &display_lock );

	generate_food( id );
}

void
__move_snake ( int id, pos_T *next_pos )
{
	pthread_mutex_lock( &display_lock );

	// Push pos into snake and plot this new point.
	mvwprintw( params.map[id], next_pos->y, next_pos->x, SNAKE_ICON ); 
	enqueue( &snakeQueue[id], next_pos );

	// Pop tail of snake and erase that point.
	pos_T popObj;
	dequeue( &snakeQueue[id], &popObj );
	mvwprintw( params.map[id], popObj.y, popObj.x, " " ); 
	wrefresh( params.map[id] );

	pthread_mutex_unlock( &display_lock );
}

pos_T 
get_next_pos ( pos_T *pos, DIRECTION direction )
{
	pos_T next_pos = *pos;
	switch ( direction )
	{
		case DIRECTION_UP    : next_pos.y--; break;
		case DIRECTION_DOWN  : next_pos.y++; break;
		case DIRECTION_LEFT  : next_pos.x--; break;
		case DIRECTION_RIGHT : next_pos.x++; break;
		default : error_check ( true, "Invalid direction.\n" );
	}
	return next_pos;
}

bool
is_hit_grid ( int id, pos_T *next_pos )
{
	if ( next_pos->x == params.N || next_pos->x == -1 || next_pos->y == params.M || next_pos->y == -1 )
		return true;
	for ( int i = 0; i < snakeQueue[id].size; i++ )	
	{
		pos_T snake_pos = snakeQueue[id].pos[(i + snakeQueue[id].head) % snakeQueue[id].BUFSIZE];
		if ( next_pos->x == snake_pos.x && next_pos->y == snake_pos.y )
			return true;
	}
	return false;
}

void
gameover ( int id )
{
	params.is_die[id] = true;

	pthread_mutex_lock( &display_lock );
	for ( int i = 0; i < params.M; ++i )
		for ( int j = 0; j < params.N; ++j )
			mvwprintw( params.map[id], i, j, "-" );
	mvwprintw( params.map[id], params.M/2-1, params.N/2-4, "Game Over" );
	wrefresh( params.map[id] );
	pthread_mutex_unlock( &display_lock );

	// Check if all snake is die.
	bool all_die = true;
	for ( int i = 0; i < params.playerNum; i++ )
		all_die &= params.is_die[i];
	if ( all_die )
		raise( SIGUSR1 );

	pthread_exit(NULL);
}

bool player_i_own_this_key ( int id, int key )
{
	if ( id == 0 && (key == 'w' || key == 's' || key == 'a' || key == 'd' || key == 'r' || key == 'f') )
		return true;
	if ( id == 1 && (key == 'i' || key == 'k' || key == 'j' || key == 'l' || key == 'y' || key == 'h') )
		return true;
	if ( id == 2 && (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT || key == KEY_PPAGE || key == KEY_NPAGE) )
		return true;
	return false;
}

DIRECTION 
get_next_direction ( int id, int key )
{
	switch( key )
	{
		case 'w': case 'i': case KEY_UP  : return DIRECTION_UP;
		case 's': case 'k': case KEY_DOWN: return DIRECTION_DOWN;
		case 'a': case 'j': case KEY_LEFT: return DIRECTION_LEFT;
		case 'd': case 'l': case KEY_RIGHT:return DIRECTION_RIGHT;
	}
	error_check( true, "Invalid key in 'get_next_direction'.\n" );
	return 0;
}

void
generate_food ( int id )
{
	if ( snakeQueue[id].size == snakeQueue[id].BUFSIZE )
		gameover(id);
	pos_T pos;
	while ( true )
	{
		pos.x = get_thread_safe_rand( OP_RAND, id + 1 ) % (params.N);
		pos.y = get_thread_safe_rand( OP_RAND, id + 1 ) % (params.M);

		bool is_legal_pos = true;
		for ( int i = 0; i < snakeQueue[id].size; i++ )	
		{
			pos_T snake_pos = snakeQueue[id].pos[(i + snakeQueue[id].head) % snakeQueue[id].BUFSIZE];
			if ( pos.x == snake_pos.x && pos.y == snake_pos.y )
			{
				is_legal_pos = false;
				break;
			}
		}
		if ( is_legal_pos )
		{
			params.food_pos[id] = pos;
			pthread_mutex_lock( &display_lock );
			init_pair( 1, COLOR_YELLOW, COLOR_BLACK );
			wattrset( params.map[id], A_BOLD | COLOR_PAIR(1) );
			mvwprintw( params.map[id], params.food_pos[id].y, params.food_pos[id].x, FOOD_ICON );
			wrefresh( params.map[id] );
			wattrset( params.map[id], 0 );
			pthread_mutex_unlock( &display_lock );
			break;
		}
	}
}

bool is_plugin_key ( int id, int key )
{
	if ( id == 0 && (key == 'r' || key == 'f') )
		return true;
	if ( id == 1 && (key == 'y' || key == 'h') )
		return true;
	if ( id == 2 && (key == KEY_PPAGE || key == KEY_NPAGE) )
		return true;
	return false;
}

void
add_plugin ( int id, int key )
{
	switch( key )
	{
		case 'r': case 'y': case KEY_PPAGE: 
			params.timeout[id] /= 2; 
			params.scoreStep[id] *= 2;
			return;
		case 'f': case 'h': case KEY_NPAGE: 
			params.timeout[id] *= 2;
			params.scoreStep[id] /= 2;
			return;
	}
	error_check( true, "Invalid key in 'add_plugin'.\n" );
}

void 
set_seeds ()
{
	if ( params.usermode == true )
	{
		for ( int i = 0; i < params.playerNum; ++i )
			usermode_thread_rand( OP_SEED, i + 1 );
	}
	else
	{
		for ( int i = 0; i < params.playerNum; ++i )
			if ( syscall( SYS_THREAD_RAND, OP_SEED, i + 1 ) == -1 )
			{
				perror( "SYSTEM CALL ERROR" );
				exit(1);
			}
	}
}

DIRECTION
get_opposite_direction ( int origin )
{
	if ( origin == DIRECTION_UP )
		return DIRECTION_DOWN;
	if ( origin == DIRECTION_DOWN )
		return DIRECTION_UP;
	if ( origin == DIRECTION_LEFT )
		return DIRECTION_RIGHT;
	if ( origin == DIRECTION_RIGHT )
		return DIRECTION_LEFT;
	error_check( true, "get_opposite_direction error!\n" );
	return 0;
}

int 
usermode_thread_rand ( int op, int idx )
{
	if ( op == OP_SEED )
	{
		usermode_seeds[idx - 1] = (time(NULL) + getpid()) >> idx;
		return 0;
	}
	usermode_seeds[idx - 1] = usermode_seeds[idx - 1] * 1103515245 + 12345;
    return ((unsigned)(usermode_seeds[idx - 1]/65536) % 32768);
}

int 
get_thread_safe_rand ( int op, int idx )
{
	if ( params.usermode == true )
		return usermode_thread_rand( op, idx );

	int num = syscall( SYS_THREAD_RAND, op, idx );
	if ( num == -1 )
	{
		perror( "SYSTEM CALL ERROR" );	
		exit(1);
	}
	return num;
}
