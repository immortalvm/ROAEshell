/*
    Immortal Database EUROSTART project

    This is a modified version of the file distributed with sqlite3,
    that adds a mini-API aimed at executing sqlite shell commands
    from the ROAE shell
    
    See https://www.sqlite.org/copyright.html for the original copyright
    
    July, 2023 - May 2024
*/

  // Init sqlite shell
  void IDA_SQLITE_shell_init();
  // Run an internal sqlite command, that is, those that start with "."
  int IDA_SQLITE_do_meta_command(char *cmd);
  // Run an SQL command
  int IDA_SQLITE_shell_exec(char *cmd);
  // Run an internal command or SQL command depending on whether it starts with "."
  int IDA_SQLITE_run(char *cmd);
  // Run an sequence of internal or SQL commands separated by "\n" (without blanks) 
  int IDA_SQLITE_run_sequence(char *cmd);
  
  // Include sqlite3 shell stuff w/o main routine
  #ifndef main 
  #define main __no_main__
  #endif
  #include "shell.c"
  #undef main

  static ShellState IDA_SQLITE_data;

  void IDA_SQLITE_shell_init()
  {
    ShellState *s = &IDA_SQLITE_data;
    main_init(s);
    s->out = stdout;
  }

  // Run an internal sqlite command, that is, those that start with "."
  int IDA_SQLITE_do_meta_command(char *cmd)
  {
    ShellState *s = &IDA_SQLITE_data;
    int rc = SQLITE_ERROR;
    char *cmd_dup = strdup(cmd); // Duplicate as it can be modified when parsed
    if (!cmd_dup) return SQLITE_ERROR;

    rc = do_meta_command(cmd_dup, s);
    free(cmd_dup);
    return rc;
  }

  // Run an SQL command
  int IDA_SQLITE_shell_exec(char *cmd) {
    char *cmd_dup = strdup(cmd); // Duplicate as it can be modified when parsed
    if (!cmd_dup) return SQLITE_ERROR;

    ShellState *s = &IDA_SQLITE_data;
    char *zErrMsg = 0;
    open_db(s, 0);
    int rc = shell_exec(s, cmd, &zErrMsg);
    if (zErrMsg || rc) {
      if (zErrMsg != 0) {
        utf8_printf(stderr, "Error: %s\n", zErrMsg);
      } else {
        utf8_printf(stderr, "Error: unable to process SQL: %s\n", cmd);
      }
      rc = (rc != 0) ? rc : 1;
    }
    if (zErrMsg) sqlite3_free(zErrMsg);
    free(cmd_dup);
    return rc;
  }

  // Run an internal command or SQL command depending on whether it starts with "."
  int IDA_SQLITE_run(char *cmd)
  {
    if (cmd) {
        int ret;
        while (*cmd && isblank(*cmd)) cmd++; // ltrim(cmd)
        if ('.' == cmd[0]){
            // Sqlite shell command, starting by "."
            ret = IDA_SQLITE_do_meta_command(cmd);
        }
        else {
            // SQL statement
            ret = IDA_SQLITE_shell_exec(cmd);
        }
        return ret;
    }
    return SQLITE_ERROR;
  }

  // Run an sequence of internal sqlite shell commands or SQL commands
  // Input string is separated in a list of command string using "\n" as separator 
  // If a command fails, the execution is stopped and returns error
  // else the return value of the last command is returned
  int IDA_SQLITE_run_sequence(char *cmd)
  {
    char *p, *q;
    int ret, end;
    if (!cmd) return SQLITE_ERROR;

    char *cmd_dup = strdup(cmd); // Duplicate as it can be modified below
    if (!cmd_dup) return SQLITE_ERROR;

    p = cmd_dup;
    q = cmd_dup;
    end = 0;
    while (!end) {
        switch (*q) {
            case '\0':
                ret = IDA_SQLITE_run(p);
                end = 1;
                break;
            case '\n':
                *q = '\0';
                if (*p) { // spkip multiple \n
                    ret = IDA_SQLITE_run(p);
                    if (ret) {
                      end = 1;
                    }
                }
                p=++q;
                break;
            default:
                q++;
                break;
        }
    }
    free(cmd_dup);
    return ret;
  }

  /* use this main() is for testing IDA API; compile with one of these:
       ivm64-gcc -DSQLITE_OMIT_LOAD_EXTENSION -Dmain_ida_test=main ida_sqlite3.c ./run-ivm64/lib/libsqlite3.a
       ivm64-gcc -DSQLITE_OMIT_LOAD_EXTENSION -Dmain_ida_test=main ida_sqlite3.c -L ./run-ivm64/lib/ -lsqlite3
       gcc  -DSQLITE_OMIT_LOAD_EXTENSION -Dmain_ida_test=main ida_sqlite3.c ./run-linux/lib/libsqlite3.a
       gcc  -DSQLITE_OMIT_LOAD_EXTENSION -Dmain_ida_test=main ida_sqlite3.c -L ./run-linux/lib/ -lsqlite3
   */
  int SQLITE_CDECL main_ida_test(int argc, char **argv)
  {
    printdebug("*** IDA [%s]\n", __func__);

    IDA_SQLITE_shell_init();

    int rc = 0;
    rc = IDA_SQLITE_do_meta_command(".help\n");
    rc |= IDA_SQLITE_run(".open db/simpledb.db\n");
    rc |= IDA_SQLITE_do_meta_command(".databases\n");
    rc |= IDA_SQLITE_do_meta_command(".tables\n");


    rc |= IDA_SQLITE_do_meta_command(".mode table\n");
    rc |= IDA_SQLITE_do_meta_command(".headers on\n");
    rc |= IDA_SQLITE_shell_exec("SELECT * FROM users;\n");

    rc |= IDA_SQLITE_shell_exec("SELECT 'hello, what''s up? ';");

    // Testing newline-separated sequences
    rc |= IDA_SQLITE_run_sequence("SELECT 'begin empty sequence'\nselect 'end empty sequence'");
    rc |= IDA_SQLITE_run_sequence("SELECT 'begin sequence ii'\n.noexists\nSELECT 'end sequence ii';");
    rc |= IDA_SQLITE_run_sequence("\t  \n\n;\n  \t\n");
    rc |= IDA_SQLITE_run_sequence("SELECT 'begin sequence iii'\n"
                                  ".parameter set ?1 A\n\n\n"
                                  ".parameter set ?2 A\n\n"
                                  " \t .parameter set ?3 C\n"
                                  ".parameter list  \nselect 'bye'; select 'bye iii';");

    return rc;
  }
