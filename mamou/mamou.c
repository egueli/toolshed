/***************************************************************************
 * mamou.c: command line parsing routines and assembler engine
 *
 * $Id$
 *
 * The Mamou Assembler - A Hitachi 6309 assembler
 *
 * (C) 2004 Boisy G. Pitre
 ***************************************************************************/


#include "mamou.h"


/* Static functions. */

static void mamou_assemble(assembler *as);
static void mamou_initialize(assembler *as);
static void mamou_deinitialize(assembler *as);
static void mamou_pass(assembler *as);


/*
 * main:
 *
 * The main entry point.
 */
 
int main(int argc, char **argv)
{
	char			*p;
	char			*i;
	int				j = 0;
    int				v;
	assembler		as;
	
	
	/* 1. Initialize our globals. */
	
    as.Argv = argv;

    init_globals(&as);

 
	/* 2. Display help, if necessary. */
	
	if (argc < 2)
    {
		fprintf(stderr, "The Mamou Assembler\n");
		fprintf(stderr, "Copyright (C) 2004 Boisy G. Pitre\n");
		fprintf(stderr, "Syntax: mamou [<opts>] [file)] [<opts>]\n");
        fprintf(stderr, "Function: 6809/6309 assembler\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "    -a<sym>[=<val>] assign val to sym\n");
        fprintf(stderr, "    -b              no binary image file output\n");
        fprintf(stderr, "    -c              cross reference output\n");
        fprintf(stderr, "    -d              debug mode\n");
        fprintf(stderr, "    -e              extended 6309 instruction mode\n");
        fprintf(stderr, "    -h              Intel hex file output\n");
        fprintf(stderr, "    -i[=]<dir>      additional include directories\n");
        fprintf(stderr, "    -l              list file\n");
        fprintf(stderr, "    -ls             source only list\n");
        fprintf(stderr, "    -lt             use tabs instead of spaces\n");
        fprintf(stderr, "    -m              Disk BASIC mode (non-OS-9 assembly behavior)\n");
        fprintf(stderr, "    -np             supress 'page' pseudo output\n");
        fprintf(stderr, "    -o[=]<file>     output to file\n");
        fprintf(stderr, "    -p              don't assemble, just parse\n");
        fprintf(stderr, "    -q              quiet mode\n");
        fprintf(stderr, "    -s              symbol table dump\n");
        fprintf(stderr, "    -x              suppress warnings and errors\n");
        fprintf(stderr, "    -y              include instruction cycle count\n");
        fprintf(stderr, "    -z              suppress conditionals in assembly list output\n");

        exit(1);
    }


    /* 3. Parse command line for options */

    for (j = 1; j < argc; j++)
    {
        if (*argv[j] == '-')
        {
            switch (tolower(argv[j][1]))
            {
                case 'a':
                    /* 1. Assembly define specification */
					
                    p = &argv[j][2];

                    if (*p == '\0')
                    {
                        /* 1. No symbol */
						
                        break;
                    }
					
                    i = p;

                    while (*i != '=' && *i != '\0')
                    {
                        i++;
                    }

                    /* now i points to '=' or \0 */
                    if (*i == '=')
                    {
                        *i = '\0';
                        i++;
                        v = atoi(i);
                    }
                    else
                    {
                        v = 1;
                    }
                    /* symbol_add value */
                    symbol_add(&as, p, v, 0);
                    break;
					
                case 'b':
                    /* Binary file output */
                    as.Binfil = 0;
                    break;	
					
                case 'e':
                    /* 6309 extended instruction mode */
                    as.h6309 = 1;
                    break;	
					
                case 'h':
                    /* Hex file output */
                    as.Hexfil = 1;
                    break;
					
                case 'i':
                    /* include directive */
                    if (as.include_index + 1 == INCSIZE)
                    {
                        /* reached our capacity */
                        break;
                    }
                    p = &argv[j][2];
                    if (*p == '=')
                    {
                        p++;
                    }
                    as.includes[as.include_index++] = p;
                    break;
					
                case 'l':
                    /* Listing file */
                    if (tolower(argv[j][2]) == 's')
                    {
                        as.o_format_only = BP_TRUE;
                        as.Opt_W = 256;
                    }
                    if (tolower(argv[j][2]) == 't')
                    {
                        as.tabbed = 1;
                    }
                    as.o_show_listing = 1;
                    break;
					
                case 'm':
                    as.o_decb = 1;
                    break;
					
                case 'o':
                    /* Output file */
                    p = &argv[j][2];
                    if (*p == '=')
                    {
                        p++;
                    }
                    strncpy(as.object_name, p, FNAMESIZE - 1);
                    break;
					
                case 'p':
                    /* Parse only output */
                    as.Preprocess = BP_FALSE;
                    break;
					
                case 'q':
                    /* quiet mode */
                    as.o_quiet_mode = BP_TRUE;
                    break;
					
                case 's':
                    /* Symbol table dump */
                    as.o_show_symbol_table = BP_TRUE;
                    break;
					
                case 'd':
                    /* o_debug mode */
                    as.o_debug = 1;
                    break;
					
                case 'c':
                    /* Cross ref output */
                    as.CREflag = 1;
                    break;
					
                case 'x':
                    /* Suppress errors and warnings */
                    as.SuppressFlag = 1;
                    break;
					
                case 'y':
                    /* Cycle count (sort of works) */
                    as.Cflag = 1;
                    break;
					
                case 'z':
                    as.Opt_C = BP_FALSE;
                    break;
                    
                default:
                    /* Bad option */
                    fprintf(stderr, "Unknown option\n");
                    exit(0);
            }
        }
        else if (as.file_index + 1 < MAXAFILE)
        {
            as.file_name[as.file_index++] = argv[j];
        }
    }
	
	
	/* 4. Call the assembler to do its work. */

	mamou_assemble(&as);
	
	
	/* 5. Return status. */
	
	return 0;
}



/*
 * mamou_assemble:
 *
 * This is the heart of the assembly process.
 */

void mamou_assemble(assembler *as)
{
	/* 1. Initialize the assembler for the first pass. */
	
	as->pass = 1;
	
    mamou_initialize(as);


	/* 2. For each file we have to assemble... */
	
    for (as->current_filename_index = 1; as->current_filename_index <= as->file_index; as->current_filename_index++)
    {
		/* 1. Open a path to the file. */
		
        if (_coco_open(&(as->file_stack[as->file_stack_index].fd), as->file_name[as->file_index - 1], FAM_READ) != 0)
        {
            printf("mamou: can't open %s\n", as->file_name[as->file_index - 1]);

            return;
        }


		/* 2. Copy the file name. */
		
		strncpy(as->file_stack[as->file_stack_index].file, as->file_name[as->file_index - 1], FNAMESIZE);


		/* 3. Reset the line counters. */

		as->file_stack[as->file_stack_index].current_line = 0;
		as->file_stack[as->file_stack_index].num_blank_lines = 0;
		as->file_stack[as->file_stack_index].num_comment_lines = 0;


		/* 4. Make the first pass. */
		
		mamou_pass(as);

//		_coco_close(as->file_stack[as->file_stack_index].fd);
    }


	/* 3. If the assembly pass above yielded no errors... */
	
    if (as->num_errors == 0)
    {
		/********** SECOND PASS **********/
		
		
		/* 1. Increment the pass. */
		
        as->pass++;
		
		
		/* 2. Re-initialize the assembler. */
		
        mamou_initialize(as);


		/* 3. Reset the file stack index. */

        as->file_stack_index = 0;
		

		/* 4. If this is a DECB .BIN file, emit the initial header. */
		
		if (as->o_decb == BP_TRUE && as->orgs[as->current_org].size > 0)
		{
			decb_header_emit(as, as->orgs[as->current_org].org, as->orgs[as->current_org].size);
		}
		
		as->current_org++;
		
		
		/* 5. Walk the file list again... */
		
        for (as->current_filename_index = 1; as->current_filename_index <= as->file_index; as->current_filename_index++)
        {
			/* 1. Open the current file. */
			
            if (_coco_open(&(as->file_stack[as->file_stack_index].fd), as->file_name[as->file_index - 1], FAM_READ) == 0)
            {
				/* 1. Copy the filename. */
				
                strncpy(as->file_stack[as->file_stack_index].file, as->file_name[as->file_index - 1], FNAMESIZE);


				/* 2. Reset the line counters. */
				
                as->file_stack[as->file_stack_index].current_line = 0;
                as->file_stack[as->file_stack_index].num_blank_lines = 0;
                as->file_stack[as->file_stack_index].num_comment_lines = 0;


				/* 3. Make a pass... */
				
                mamou_pass(as);
		//		_coco_close(as->file_stack[as->file_stack_index].fd);
            }
        }
		

		/* Emit Disk BASIC trailer. */
		
		if (as->o_decb == BP_TRUE)
		{
			decb_trailer_emit(as, 0xEEAA);
		}

		
		/* 1. Do we show the symbol table? */
		
        if (as->o_show_symbol_table == BP_TRUE)
        {
            printf("\f");
            stable(as->bucket);
            printf("\n");
        }
        
        if (as->CREflag == 1)
        {
            printf("\f");
            cross(as->bucket);
        }

        finish_outfile(as);
    }


    if ((as->o_quiet_mode == BP_FALSE) && (as->o_format_only == 0))
    {
        report_summary(as);
    }


	/* Terminate the forward reference file. */

    fwd_deinit(as);


    /* Added to remove an object if there were errors in the assembly - BGP 2002/07/25 */

    if (as->num_errors != 0)
    {
        _coco_delete(as->object_name);
    }


	/* 1. Deinitialize the assembler. */
	
    mamou_deinitialize(as);


	/* Return. */

    return;
}



static void mamou_initialize(assembler *as)
{
    if (as->o_debug)
    {
        printf("Initializing for pass %d\n", as->pass);
    }

	if (as->pass == 1)
	{
		/* Pass 1 initialization. */

		as->num_errors				= 0;
		as->data_counter			= 0;
		as->program_counter			= 0;
		as->pass					= 1;
		as->Ctotal					= 0;
		as->N_page					= 0;
		as->input_line[MAXBUF-1]	= '\n';
		as->file_stack_index		= 0;
		as->file_stack[0].current_line  = 0;
		as->file_stack[0].num_blank_lines = 0;
		as->file_stack[0].num_comment_lines = 0;
		
		as->conditional_stack_index = 0;
		as->conditional_stack[0]	= 1;

		as->current_org = 0;
		as->orgs[as->current_org].org = 0;
		as->orgs[as->current_org].size = 0;

		if (as->object_name[0] != EOS)
		{
			if (as->o_quiet_mode == BP_FALSE)
			{
				printf("output:  %s\n", as->object_name);
			}

			if ((as->fd_object = fopen(as->object_name, "wb")) == NULL)
			{
				fatal("Can't create object file");
			}
		}


		fwd_init(as);		/* forward ref init */
		local_init();		/* target machine specific init. */
		env_init(as);		/* environment variables init. */
	}
	else
	{
		/* Pass 2 initialization. */

		as->data_counter    = 0;
		as->program_counter	= 0;
		as->DP				= 0;
		as->allow_warnings	= 0;
		as->E_total			= 0;
		as->P_total			= 0;
		as->Ctotal			= 0;
		as->N_page			= 0;
		as->file_stack[0].current_line = 0;
		as->file_stack[0].num_blank_lines = 0;
		as->file_stack[0].num_comment_lines = 0;

		as->current_org		= 0;
		
		fwd_reinit(as);

		as->conditional_stack_index = 0;
		as->conditional_stack[as->conditional_stack_index] = 1;
	}
	

    return;
}



static void mamou_deinitialize(assembler *as)
{
    if (as->o_debug)
    {
        printf("Deinitializing\n");
    }
	

    return;
}



static void mamou_pass(assembler *as)
{
    if (as->o_debug)
    {
        printf("\n------");
        printf("\nPass %d", as->pass);
        printf("\n------\n");
    }

    while (as->file_stack_index >= 0)
    {
        int size = MAXBUF - 1;

        while (_coco_readln(as->file_stack[as->file_stack_index].fd, as->input_line, &size) == 0)
        {
            char *p = strchr(as->input_line, 0x0D);
			BP_int32		line_type;
			
			
            size = MAXBUF - 1;
            if (p != NULL)
            {
#ifdef _WIN32
                p++;
                *p = 0x0A;
#else
                *p = 0x0A;
#endif
                p++;
                *p = '\0';
            }

            as->file_stack[as->file_stack_index].current_line++;
            as->P_force = 0;	/* No force unless bytes emitted */
            as->N_page = 0;
	
			line_type = mamou_parse_line(as);
			
			if (line_type == 2 && as->Preprocess == BP_TRUE)
            {
                process(as);
            }
            else
            {
                print_line(as, 0, ' ', 0);

				if (line_type == 0)
				{
					as->file_stack[as->file_stack_index].num_blank_lines++;
				}
				else
				{
					as->file_stack[as->file_stack_index].num_comment_lines++;
				}
            }
            as->P_total = 0;	/* reset byte count */
            as->cumulative_cycles = 0;	/* and per instruction cycle count */
        }
        _coco_close(as->file_stack[as->file_stack_index].fd);
        if ((as->pass == 2) && as->file_stack_index > 0)
        {
            as->file_stack[as->file_stack_index-1].current_line = as->file_stack[as->file_stack_index].current_line;
        }
        as->file_stack_index--;
    }

    f_record(as);
}



/*
 * mamou_parse_line: split input line into label, op and operand
 *
 * Returns: 0 if a blank line, 1 if a comment, 2 if an actual line
 */
 
int mamou_parse_line(assembler *as)
{
    register char *ptrfrm = as->input_line;
    char *ptrto = as->label;
    static char hold_lbl[80];
    static int cont_prev = 0;
    register struct oper *i;


    *as->label = EOS;
    *as->Op = EOS;
    *as->operand = EOS;
    *as->comment = EOS;


	/* 1. First, check to see if this is a blank line. */
	
	while (isspace(*ptrfrm)) ptrfrm++;
	
	if (*ptrfrm == '\n' || *ptrfrm == EOS)
	{
		*ptrto = EOS;

		return 0;
	}
	
	
	/* 2. Reanchor pointer to start of line. */
	
	ptrfrm = as->input_line;
	
    if (*ptrfrm == '*' || *ptrfrm == '\n' ||
        *ptrfrm == ';' || *ptrfrm == '#')
    {
        strcpy(as->comment, as->input_line);
        ptrto = as->comment;

        while (!eol(*ptrto))
        {
            ptrto++;
        }

        *ptrto = EOS;

        return 1;	/* a comment line */
    }

    while (delim(*ptrfrm) == BP_FALSE)
    {
        *ptrto++ = *ptrfrm++;
    }

    if (ptrto > as->label && *--ptrto != ':')
    {
        ptrto++;     /* allow trailing : */
    }

    *ptrto = EOS;

    ptrfrm = skip_white(ptrfrm);

    ptrto = as->Op;

    while (delim(*ptrfrm) == BP_FALSE)
    {
        *ptrto++ = mapdn(*ptrfrm++);
    }

    *ptrto = EOS;

    ptrfrm = skip_white(ptrfrm);

    /* determine whether this op code has a parameter */
    i = mne_look(as, as->Op);
    if (i != NULL)
    {
        if ((i->class == PSEUDO && i->cycles & 1 == 1) ||
            (i->class != INH && i->class != P2INH && i->class != P3INH) )
        {
            ptrto = as->operand;
            if (i->class == PSEUDO && i->cycles == 0x2)
            {
                char fccdelim;

                /* delimiter pseudo op (fcs/fcc) */
                fccdelim = *ptrfrm;
                do
                {
                    *ptrto++ = *ptrfrm++;
                } while (*ptrfrm != EOS && *ptrfrm != fccdelim);
                *ptrto++ = *ptrfrm++;
            }
            else if (i->class == PSEUDO && i->cycles == 0x4)
            {
                /* pseudo op has spaces in operand */
                do
                {
                    *ptrto++ = *ptrfrm++;
                } while (*ptrfrm != EOS && !eol(*ptrfrm));
            }
            else
            {
                while (delim(*ptrfrm) == BP_FALSE)
                {
                    *ptrto++ = *ptrfrm++;
                }
            }
            *ptrto = EOS;

            ptrfrm = skip_white(ptrfrm);
        }
    }

    ptrto = as->comment;
    while (!eol(*ptrfrm))
    {
        *ptrto++ = *ptrfrm++;
    }
    *ptrto = EOS;

/* Below added by GFC 8/30/94 */

    if (cont_prev)
    {
            cont_prev = 0;
            strcpy(as->label, hold_lbl);
    }

    if (as->Op[0] == ';')
    {
        if (as->label[0] == '\0')
        {
            return 0;	/* a comment line */
        }
        else	/* save this label for the next mamou_parse_line() */
        {
            strcpy(hold_lbl, as->label);
            cont_prev = 1;

            return 1;	/* a comment line */
        }
    }


    if (as->o_debug)
    {
        printf("\n");
        printf("Label      %s\n", as->label);
        printf("Op         %s\n", as->Op);
        printf("Operand    %s\n", as->operand);
    }


    return 2;
}


/*
 *	process --- determine mnemonic class and act on it
 */
void process(assembler *as)
{
    register struct oper *i;

    as->old_program_counter = as->program_counter;		/* setup `old' program counter */
    as->optr = as->operand; 	/* point to beginning of operand field */

    if (as->conditional_stack[as->conditional_stack_index] == 0)
    {
        /* we should ignore this line unless it's an endc */
        i = mne_look(as, as->Op);
        if (i != NULL && i->class == PSEUDO)
        {
            i->func(as);
        }
        return;
    }

    if (*as->Op == EOS)
    {
        /* no mnemonic */
        if (*as->label != EOS)
        {
            symbol_add(as, as->label, as->program_counter, 0);
            print_line(as, 0, ' ', 0);
        }
    }
    else if ((i = mne_look(as, as->Op)) == NULL)
    {
        error(as, "Unrecognized Mnemonic");
    }
    else if (i->class == PSEUDO)
    {
        i->func(as);
        if (as->E_total >= E_LIMIT)
        {
            f_record(as);
        }

    }
    else
    {
        if (*as->label)
        {
            symbol_add(as, as->label, as->program_counter, 0);
        }
        if (as->Cflag)
        {
            as->cumulative_cycles = i->cycles;
            if (as->h6309)
            {
                as->cumulative_cycles--;
            }
        }
        i->func(as, i->opcode);
        if (as->E_total >= E_LIMIT)
        {
            f_record(as);
        }
        
        if (as->Cflag)
        {
            as->Ctotal += as->cumulative_cycles;
        }
    }
}


void init_globals(assembler *as)
{
    as->num_errors = 0;		/* total number of errors       */
    as->input_line[0] = 0;		/* input line buffer            */
    as->label[0] = 0;		/* label on current line        */
    as->Op[0] = 0;		/* opcode mnemonic on current line      */
    as->operand[0] = 0;		/* remainder of line after op           */
    as->comment[0] = 0;		/* comment after operand or line        */
    as->optr = 0;		/* pointer into current Operand field   */
    as->force_word = 1;		/* Result should be a word when set     */
    as->force_byte = BP_FALSE;	/* Result should be a byte when set     */
    as->program_counter = 0;			/* Program Counter              */
    as->DP = 0;			/* Direct Page                  */
    as->allow_warnings = 0;		/* allow_warningss                     */
    as->num_warnings = 0;		/* total warnings               */
    as->old_program_counter = 0;		/* Program Counter at beginning */

    as->last_symbol = 0;		/* result of last symbol_find        */

    as->pass = 1;		/* Current pass #               */
//    as->file_count = 0;		/* Number of files to assemble  */
    as->Ffn = 0;		/* forward ref file #           */
    as->F_ref = 0;		/* next line with forward ref   */
    as->Argv = 0;		/* pointer to file names        */

    as->E_total = 0;		/* total # bytes for one line   */
    as->E_bytes[0] = 0;		/* Emitted held bytes           */
    as->E_pc = 0;		/* Pc at beginning of collection*/

    as->P_force = 0;		/* force listing line to include Old_pc */
    as->P_total = 0;		/* current number of bytes collected    */
    as->P_bytes[0] = 0;		/* Bytes collected for listing  */

    as->cumulative_cycles = 0;		/* # of cycles per instruction  */
    as->Ctotal = 0;		/* # of cycles seen so far */
    as->N_page = 0;		/* new page flag */
    as->page_number = 2;		/* page number */
    as->CREflag = 0;		/* cross reference table flag */
    as->Cflag = 0;		/* cycle count flag */
    as->Opt_C = BP_TRUE;		/* show conditionals in listing */
    as->o_page_depth = 66;
    as->o_show_error = BP_TRUE;
    as->Opt_F = BP_FALSE;
    as->Opt_G = BP_FALSE;
    as->o_show_listing = 0;		/* listing flag 0=nolist, 1=list*/
    as->o_decb			= BP_FALSE;
    as->Opt_N = BP_FALSE;
    as->o_quiet_mode = BP_FALSE;
    as->o_show_symbol_table = BP_FALSE;		/* symbol table flag, 0=no symbol */
    as->Opt_W = 80;
    as->o_debug = 0;		/* debug flag */
    as->Binfil = 1;		/* binary image file output flag */
    as->Hexfil = 0;		/* Intel Hex file output flag */
    as->fd_object = NULL;		/* object file's file descriptor*/
    as->object_name[0] = EOS;
    as->bucket = NULL;
    as->file_stack_index = 0;
    as->do_module_crc = BP_FALSE;
    as->_crc[0] = 0xFF;
    as->_crc[1] = 0xFF;
    as->_crc[2] = 0xFF;
    as->accum = 0x00ffffff;
    as->Preprocess = BP_TRUE;
    as->include_index = 0;
    as->file_index = 0;
    as->current_line = 0;
    as->current_page = 1;
    as->header_depth = 3;
    as->footer_depth = 3;
    as->o_decb = 0;
    as->SuppressFlag = 0;	/* suppress errors and warnings */
    as->tabbed = 0;
    as->h6309 = 0;		/* assume 6809 mode only */

    return;
}
