/*
    Immortal Database EUROSTART project

    This is a modified version of the file distributed with sqlite3,
    that adds a mini-API aimed at executing sqlite shell commands
    from the ROAE shell
    
    See https://www.sqlite.org/copyright.html for the original copyright
    
    July, 2023 - Oct. 2023
*/

  // Init sqlite shell
  void IDA_SQLITE_shell_init();
  // Run an internal sqlite command, that is, those that start with "."
  int IDA_SQLITE_do_meta_command(char *cmd);
  // Run an SQL command
  int IDA_SQLITE_shell_exec(char *cmd);
  // Run an internal command or SQL command depending on whether it starts with "."
  int IDA_SQLITE_run(char *cmd);
  
  
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
    return do_meta_command(cmd, s);
  }

  // Run an SQL command
  int IDA_SQLITE_shell_exec(char *cmd)
  {
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
      sqlite3_free(zErrMsg);
      return rc != 0 ? rc : 1;
    }
  }

  // Run an internal command or SQL command depending on whether it starts with "."
  // It duplicates the command string in order to use a writable buffer
  int IDA_SQLITE_run(char *cmd)
  {
    if (cmd) {
        char *c = strdup(cmd);
        if (c) {
            int ret;
            if (c[0]=='.' ){
                // Sqlite shell command, starting by "."
                ret = IDA_SQLITE_do_meta_command(c);
            }
            else {
                // SQL statement
                ret = IDA_SQLITE_shell_exec(c);
            }
            free(c);
            return ret;
        }
    }
    return SQLITE_ERROR;
  }

  /* use this main() is for testing IDA API */
  int SQLITE_CDECL main_ida_test(int argc, char **argv)
  {
    printdebug("*** IDA [%s]\n", __func__);

    IDA_SQLITE_shell_init();

    int rc = IDA_SQLITE_do_meta_command(".help\n");
    rc |= IDA_SQLITE_do_meta_command(".open db/database.db\n");
    rc |= IDA_SQLITE_do_meta_command(".databases\n");
    rc |= IDA_SQLITE_do_meta_command(".tables\n");

    rc |= IDA_SQLITE_do_meta_command(".mode table\n");
    rc |= IDA_SQLITE_do_meta_command(".headers on\n");
    rc |= IDA_SQLITE_shell_exec("SELECT * FROM people;\n");

    rc |= IDA_SQLITE_shell_exec("SELECT 'hello, what''s up? ';");

    return rc;
  }
