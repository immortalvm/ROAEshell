/*
    libroae - Simple management of ROAE files 

    Immortal Database Access (iDA) EUROSTARS project

    Eladio Gutierrez, Sergio Romero, Oscar Plata
    University of Malaga, Spain

    Aug 2023
*/

// Compile for ivm:
//   ivm64-g++ lib.cpp -c ; ivm64-gcc libroae.o main.c -lstd++
// or:
//   ivm64-gcc main.c -c ; ivm64-g++ main.o libroae.cpp

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <iterator>
#include <algorithm>

#include <cstdio>
#include <cstring>
#include <cstdarg>

using namespace std;

namespace IDA_ROAE {

    class ROAE_parsing_utils{
        public:
            static string remove_comments(const string &s){
                static regex re_comment("#.*$");
                return regex_replace(s, re_comment, "");
            }

            static const string WHITESPACE;

            static string ltrim(const string &s){
                size_t start = s.find_first_not_of(WHITESPACE);
                return (start == string::npos) ? "" : s.substr(start);
            }

            static string rtrim(const string &s){
                size_t end = s.find_last_not_of(WHITESPACE);
                return (end == string::npos) ? "" : s.substr(0, end + 1);
            }

            static string trim(const string &s){
                return rtrim(ltrim(s));
            }

            static bool match_tag(const string &s, const string &tag){
                return !s.compare(tag);
            }

            static bool match_title(const string &s, string &title){
                // Match something like 'title = "this is the title"'
                regex re_title("title\\s+=\\s+(.*)$");
                std::smatch sm;    // same as std::match_results<const char*> cm;
                bool m = regex_match(s, sm, re_title);
                title = "";
                if (m) {
                    title=sm[1].str();
                }
                //cerr << "Match title '" << s << "'? " << m << "  title='" << title << "'" << endl; // Debug
                return m;
            }

            static int parse_parameter(const string &s, string &name, string &comment){
                // Match something like 'param_name - this is a comment for this param'
                static regex re_parameter("([^\\s-]*)(\\s+-\\s+(.*)){0,1}$");
                std::smatch sm;
                int m = regex_match(s, sm, re_parameter);
                name = "";
                comment = "";
                if (m) {
                    if (sm.size() >= 1) {
                        name = sm[1].str();
                    }
                    if (sm.size() >= 3) {
                        comment = sm[3].str();
                    }
                }
                return m;
            }

            // Get a command header for printing with
            // its number
            static string command_header(long i){
                std::ostringstream ss;
                ss << "----------------------" << endl;
                ss << "Command number #" << i << endl; 
                ss << "----------------------" << endl;
                return ss.str();
            }

            // Enclose string in single quotes by escaping 
            // the existing single quotes, in order to use the
            // input string in sqlite
            // Escaping ' in sqlite is doubling it: ''
            static string enclose_sqlite_single_quote(string s){
                static regex e("'");
                return "'" + regex_replace(s, e, "''") + "'";
            }

            // Remove new lines in a string; it helps to do regex matching
            // or searches
            static string remove_newlines(const string &s){
                 return std::regex_replace(s, std::regex("\\s+"), " ");
            }

    };
    // Static member declarations
    const string ROAE_parsing_utils::WHITESPACE = " \n\r\t\f\v";

    class ROAE_param {
        public:
            string name;
            string comment;
    }; 

    class ROAE_command {
        string title;
        vector<ROAE_param> param_list;
        string SQLbody;

        public:
            ROAE_command(){
                this->clear();
            }

            void clear(){
                title.clear();
                SQLbody.clear();
                param_list.clear();
            }

            void set_title(string t){
                title = ROAE_parsing_utils::trim(t);
            }

            string get_title(){
                return title;
            }

            void set_body(string body){
                SQLbody = ROAE_parsing_utils::trim(body); 
            }

            void add_param(string name, string comment){
                param_list.push_back((ROAE_param){name, comment});
            }

            long count_params(){
                return param_list.size();
            }

            // An exception can be raised if out of range
            ROAE_param get_param(long p){
                return param_list.at(p);
            }

            // Return a string with the body evaluated by
            // replacing ${{param}} by its value according the
            // passed map <parameter name, value>
            //
            // If prepared=true, return a string with the body evaluated by
            // replacing ${{param}} by the char '?' to be used 
            // in 'prepared sql statements'
            // (ref. https://en.wikipedia.org/wiki/Prepared_statement)
            string eval_param(map<string,string> parmap, bool prepared=false){
                string body = SQLbody;
                for (ROAE_param p: param_list){
                    string name = p.name;
                    try {
                        string value = "";
                        if (!prepared) { 
                            //-- // TODO: if the param is a string, it needs quotes
                            //-- // but if it is a number, no quotes are necessary
                            //-- // Nevertheless, comparing numbers and string are different in sqlite
                            //-- // The definitive solution is using sqlite prepared statements + binds
                            //-- // (if type of parameter is kown?? doe we need to check if the value
                            //-- // is numeric? which numeric (int,signed,float,...)?)
                            //-- //value = ROAE_parsing_utils::enclose_sqlite_single_quote(parmap.at(name));
    
                            // Let the user to put the quotes if needed
                            value = parmap.at(name);
                        } else {
                            value = "?";
                        }
                        regex e("\\$\\$\\{" + name + "\\}");
                        body = regex_replace(body, e, value);
                    } catch(...) {
                        cerr << "Parameter '" << name << "' not found in map" << endl;
                    };
                }
                return body;
            }

            // Return a list of params to be bound in a prepared sql statement
            // If body is like "SELECT * from table where id==$${par1}} and name==$${par2}} and $${par1} > 10"
            // we need this list [value1, value2, value1], values for parameters can be repeated
            vector<string> bind_param_list(map<string,string> parmap)
            {
                // This vector has the value or parameters found in map
                // in the order of apperance in the corresponding prepared statement
                vector<string> v;
                regex re("(^|.*?)\\$\\$\\{(.*?)\\}");

                // Replace new lines by blanks to make next stage work fine
                // (otherwise some problems arise with regexps matching newlines)
                string body = ROAE_parsing_utils::remove_newlines(SQLbody);

                smatch sm;
                long start = 0, span=body.length();

                while (start < body.length() && span > 0) {
                    string s = body.substr(start, span);
                    if(std::regex_search(s, sm, re)) {
                        //-- std::cout << "Match found\n"; // Debug
                        //-- for (size_t i = 0; i < sm.size(); ++i) {
                        //--     std::cout << i << ": '" << sm[i].str() << "'" 
                        //--             << " pos=" << sm.position() 
                        //--             << " len=" << sm.length()
                        //--             << endl;
                        //-- }
                        // group 0: the whole matching
                        // group 1: the prefix before $${...}
                        // group 2: the contents inside $${...}
                        string name = sm[2].str();
                        string value;
                        try {
                            value = parmap.at(name);
                            v.push_back(value);
                        } catch (std::exception &e) {
                            // Leave not found parameters in the template form $${name}
                            v.push_back("$${" + name + "}");
                            cerr << "Parameter '" << name << "' not found in map: " << e.what() << endl;
                        }
                    } else {
                        //std::cout << "Match not found\n"; // Debug
                        break;
                    }
                    start += sm.length();
                    span = body.length() - start;
                }
                return v;
            }

            friend std::ostream& operator<< (std::ostream &out, const ROAE_command &cmd);

            string to_string()
            {
              std::ostringstream ss;
              ss << *this; // Operator << is just overloaded
              return ss.str();
            }

    };

    std::ostream& operator<< (std::ostream &out, const ROAE_command &cmd) {
        out << "Command:"       << endl;
        out << "\ttitle = "     << cmd.title << endl;
        out << "\tParameters:"  <<  endl;
        for (ROAE_param param : cmd.param_list){
            out << "\t\t"       << param.name << " - " << param.comment << endl;
        }
        out << "\tBody:"        << endl << "\t\t" << cmd.SQLbody << endl;
        return out;
    }


    class ROAE_command_list{
        static vector<ROAE_command> command_list;

        enum roae_parsing_state {PS_NONE, PS_COMMAND, PS_TITLE, PS_PARAM, PS_BODY};

        // Add a parsed ROAE command to the command list
        // Tipical usage: add_command(command); 
        void add_command(const ROAE_command &command){
                command_list.push_back(command);
        }

        // Parse a ROAE file; return the number of commands found
        long parse_roae_file(string roaefilename){
            ifstream roaefile(roaefilename);
            enum roae_parsing_state state = PS_NONE;

            string line, title, body;
            ROAE_command command;

            while (roaefile.good()){
                getline(roaefile, line);

                //1. Remove comments and trim line
                line = ROAE_parsing_utils::remove_comments(line); 
                line = ROAE_parsing_utils::trim(line);

                //cerr << line << endl; // Debug
                //cerr << "state=" << state << endl; // Debug

                // If empty line, ignore it
                // But if we are in the body section, a command ends here
                if (!line.length()) {
                     if (PS_BODY == state){
                        state = PS_NONE; 
                        command.set_body(body);
                        //cerr << "Empty line" << endl;
                        add_command(command);
                        //cerr << "New command added; total so far " << command_list.size() << endl; // Debug
                     }
                     continue;
                }
                
                if (ROAE_parsing_utils::match_tag(line, "Command:")) {
                    // Tag "Command:" found, start a new command
                    //cerr << "** Tag Command: found, start a new command" << endl; // Debug

                    // If we are in the body section, a new command starts
                    // so add the just ended one in the list
                    if (PS_BODY == state){
                        command.set_body(body);
                        //cerr << "body set" << endl; // Debug
                        add_command(command);
                        //cerr << "New command added; total so far " << command_list.size() << endl; // Debug
                    }

                    state = PS_COMMAND;

                    title.clear();
                    body.clear();
                    command.clear();
                    // cerr << "******" << endl << "Command start" << endl << "******" << endl; //Debug
                } else {
                    //cerr <<  "Line w/o 'Command:' state=" << state << endl; //Debug
                    switch(state){
                        case PS_COMMAND:
                            //cerr << "case PS_COMMAND" << endl; //Debug
                            if (ROAE_parsing_utils::match_title(line, title)) {
                                // Tag "title=" found
                                state = PS_TITLE;
                                command.set_title(title);
                                //cerr << "** Title found:" << title << endl; //Debug
                            }
                            break;
                        case PS_TITLE:
                            //cerr << "case PS_TITLE" << endl; //Debug
                            if (ROAE_parsing_utils::match_tag(line, "Parameters:")) {
                                // Start the parameter section 
                                state = PS_PARAM;
                                //cerr << "** Parameter section found" << endl; //Debug
                            }
                            break;
                        case PS_PARAM:
                            //cerr << "case PS_PARAM" << endl; //Debug
                            // In parameter section: parse parameters if any, or start body section
                            if (ROAE_parsing_utils::match_tag(line, "Body:")) {
                                // Start body section
                                state = PS_BODY;
                            } else {
                                // Parse parameters adding them to the current ROAE command
                                string par_name, par_comment;
                                int p = ROAE_parsing_utils::parse_parameter(line, par_name, par_comment);
                                if (p) {
                                    command.add_param(par_name, par_comment); 
                                    ////cerr << "  * Parameter found: name='" << par_name
                                    //     << "' comment='" << par_comment << "'" << endl; //Debug
                                }
                            }
                            break;
                        case PS_BODY:
                            //cerr << "case PS_BODY" << endl; //Debug
                            // Finally the body; all not blank lines afer "Body:" is considered
                            // part of the body; if a blank line is found here, the current command ends
                            // (see empty line above);
                            // if a "Command:" tag is found, a new command starts
                            body += line + "\n";
                            break;
                    }
                }

            } /* while roaefile.good() */

            if (roaefile.eof()){
                // May be a command is pending to be added
                if (PS_BODY == state){
                    command.set_body(body);
                    //cerr << "EOF" << endl;
                    add_command(command);
                    //cerr << "New command added; total so far " << command_list.size() << endl; // Debug
                }
            }

            roaefile.close();
            return this->count();
        }

        public:
            // Constructor using the roae filename
            // clear the current list, and create a new one
            ROAE_command_list(string roaefilename){
                load(roaefilename);
            }

            // Constructor with no arguments, leave the
            // current command list at it is 
            ROAE_command_list(){
            }

            // Load a ROAE file, clearing the existing one
            // Return the number of commands in the list
            long load(string roaefilename){
                this->clear();
                return parse_roae_file(roaefilename);
            }

            // Clear the static list of roae commands 
            void clear() {
                command_list.clear();
            }

            // Return the number of available roae commands
            long count() {
                return command_list.size();
            }

            // Return a given command by index;
            // Index must be in range, or an exception is raised 
            ROAE_command command(long idx) {
                return command_list.at(idx);
            }

            // Return a vector with the indexes of the
            // commands in the list that match the regexp s
            vector<long> search(string s) {
                regex re(s, regex_constants::icase);
                std::smatch sm;
                vector<long> v;
                long idx = 0;
                for (ROAE_command c : command_list) {
                    string tl = c.get_title();
                    if (regex_search(tl, sm, re)){
                        v.push_back(idx);
                    }
                    idx++;
                }
                return v;
            }

            friend std::ostream& operator<< (std::ostream &out, const ROAE_command_list &cmdlist);

            string to_string()
            {
              std::ostringstream ss;
              ss << *this; // Operator << is just overloaded
              return ss.str();
            }
    };
    // Static member declarations
    vector<ROAE_command> ROAE_command_list::command_list;

    std::ostream& operator<< (std::ostream &out, const ROAE_command_list &cmdlist) {
        long n = 0;
        for(ROAE_command c : cmdlist.command_list) {
          //cout << "----------------------" << endl;
          //cout << "Command number #" << n << endl;
          //cout << "----------------------" << endl;
          cout << ROAE_parsing_utils::command_header(n);
          cout << c << endl;
          n++;
        }
        return out;
    }


} /* namespace IDA_ROAE */


/* C public API */

#ifdef __cplusplus
extern "C" {
#endif

    using namespace IDA_ROAE;

    // The global command list
    static ROAE_command_list ROAEcl;

    // Load a ROAE file and return the number of commands found
    long IDA_ROAE_load(char *filename)
    {
       return ROAEcl.load(filename);
    }

    // Delete the current roae command list
    void IDA_ROAE_clear()
    {
       ROAEcl.clear();
    }

    // Print the list of commands
    void IDA_ROAE_print_commands()
    {
       cout << ROAEcl;
    }

    // Get the number of commands
    long IDA_ROAE_count()
    {
        return ROAEcl.count();
    }

    // Print the nc-th command
    void IDA_ROAE_print_command(long nc)
    {
        try {
           cout << ROAEcl.command(nc) << endl;
        } catch (std::exception &e) {
            cerr << e.what() << endl;
        }
    }

    // Print the lis of commands whose title
    // match the regexp s
    void IDA_ROAE_search(char *re)
    {
        vector<long> v;
        v = ROAEcl.search(string(re));
        for (long idx : v){
            cout << ROAE_parsing_utils::command_header(idx);
            cout << ROAEcl.command(idx) << endl;
        }
    }

    // Eval the nc-th command with a list of nparams parameters 
    // The list of parameter values is in the argv format (last element must be NULL).
    // If values=NULL, the SQL prepared statement is returned instead.
    // If buff=NULL, a dynamic array of chars is allocated with the size of the evaluated command
    // in this case, the programmer must free it after its use
    // Return NULL if error.
    char* IDA_ROAE_eval_command(long nc, char *buff, long buffsize, char *values[])
    {
        ROAE_command cmd;
        try {
            cmd = ROAEcl.command(nc);
        } catch (...) {
            // nc out of range
            return NULL;
        }

        long nparams = cmd.count_params();
        
        // Iterate with the smallest number of the parameters of this command, and the
        // passed parameter values
        map<string, string> m;
        if (values) {
            for (long i=0; values[i] && i < nparams; i++){
                ROAE_param p = cmd.get_param(i);
                m[p.name] = values[i]; 
            }
        }

        bool prepared = (values == NULL);
        string newbody = cmd.eval_param(m, prepared);

        // The buffer needs to have enough space
        if (buff && newbody.length() >= buffsize) {
            return NULL;
        }

        if (buff) {
            strcpy(buff, newbody.c_str());
        } else {
            buff = strdup(newbody.c_str());
        }
        return buff;
    }

    // Given a list of values in argv format for parameters, return a list of
    // strings (in argv format) with the values to be replaced in a prepared
    // sql statement, in the correct order and repeated if necessary
    // Return NULL if something is wrong
    char** IDA_ROAE_command_bind_list(long nc, char *values[])
    {
        ROAE_command cmd;
        try {
            cmd = ROAEcl.command(nc);
        } catch (...) {
            // nc out of range
            return NULL;
        }

        long nparams = cmd.count_params();

        // Iterate with the smallest number of the parameters of this command, and the
        // passed parameter values
        map<string, string> m;
        if (values) {
            for (long i=0; values[i] && i < nparams; i++){
                ROAE_param p = cmd.get_param(i);
                m[p.name] = values[i];
            }
        }

        vector<string> v = cmd.bind_param_list(m);

        // The list with the values to bind in the order of appearence in the
        // prepared sql statement
        char **bind_list = (char **)malloc((1+v.size())*sizeof(char*));

        long i=0;
        for (string s: v) {
            // cerr << s << endl; // Debug
            bind_list[i++] = strdup(s.c_str()); 
        }
        bind_list[i] = NULL;
        

        // TODO: what does this one return?
        return bind_list;
    }

    // Given a list of values to be bind (in argv format) generate the
    // sequence of sqlite commands to bind those values
    // This function allocates the returned string that needs to be freed
    char* IDA_ROAE_command_bind_list_to_sqlite(char *bind_list[])
    {
        long i=0;
        string sqlite_command;
        if (bind_list) {
            sqlite_command += ".parameter clear\n"; // Separate with \n
            while (bind_list[i]) {
                sqlite_command += ".parameter set ?" + to_string(i + 1) + " " + bind_list[i] + "\n";
                i++;
            }
        }
        if (sqlite_command.empty()){
            return NULL;
        } else {
            return strdup(sqlite_command.c_str());
        }
    }

    // Get the title of the nc-th command; in case of error, return NULL
    // Note that the returned string must be deallocated
    char* IDA_ROAE_get_command_title(long nc)
    {
        try {
           return strdup(ROAEcl.command(nc).get_title().c_str());
        } catch (std::exception &e) {
            cerr << e.what() << endl;
            return NULL;
        }
    }

    // Return true if the title of the nc-th command match the regexp string r
    int IDA_ROAE_command_title_match(long nc, char* r)
    {
        try {
            regex re(r);
            cmatch cm; // Match char*
            const char *s = ROAEcl.command(nc).get_title().c_str();
            if (std::regex_search(s, cm, re)) {
                return 1;
            } else {
                return 0;
            }
        } catch (std::exception &e) {
            cerr << e.what() << endl;
            return 0;
        }
    }

    // Get the number of arguments of the nc-th command; in case of error, return -1
    long IDA_ROAE_get_command_nargs(long nc)
    {
        try {
            return ROAEcl.command(nc).count_params();
        } catch (std::exception &e) {
            cerr << e.what() << endl;
            return -1;
        }
    }

    // Get na-th argument's name of the nc-th command; in case of error, return NULL
    // Note that the returned string must be deallocated
    char* IDA_ROAE_get_command_arg_name(long nc, long na)
    {
        try {
            string argn = ROAEcl.command(nc).get_param(na).name;
            return strdup(argn.c_str());
        } catch (std::exception &e) {
            cerr << e.what() << endl;
            return NULL;
        }
    }

    // Get na-th argument's comments of the nc-th command; in case of error, return NULL
    // Note that the returned string must be deallocated
    char* IDA_ROAE_get_command_arg_comment(long nc, long na)
    {
        try {
            string argn = ROAEcl.command(nc).get_param(na).comment;
            return strdup(argn.c_str());
        } catch (std::exception &e) {
            cerr << e.what() << endl;
            return NULL;
        }
    }


/* C API tests */

static void test_cpp(char *roaefile) {
    const char *file = roaefile;

    // Testing roae command object
    cout << endl << "Testing constructing a roae command ...." << endl;
    ROAE_command c;
    c.set_title("blablabla");
    c.add_param("par1", "first param");
    c.add_param("par2", "2nd param");
    c.add_param("par3", "3rd param");
    c.set_body("SELECT * FROM table where par1==$${par1} and foo<=$${par1} and id2 <> $${par2} or id3 like $${par3};");
    cout << c;

    cout << endl << "Testing command.to_string() ...." << endl;
    cout << c.to_string() << endl;

    // Evaluating parameters
    map<string, string> m;
    m["par1"] = "123456";
    m["par2"] = "abcde";
    cout << "Evaluating params: " << endl;
    for (auto i = m.begin(); i != m.end(); i++)
		cout << i->first << "	 " << i->second << endl;
    string newbody = c.eval_param(m);
    cout << newbody << endl << endl;

    // Testing roae command list 
    cout << "------------------" << endl;
    cout << "Processing ROAE file '" << file << "'" << endl;
    cout << "------------------" << endl;
    ROAE_command_list RCL(file);

    // Print the number of parsed commands
    long ncommands = RCL.count();
    cout << "------------------" << endl;
    cout << endl << "Found " << ncommands << " roae commands" << endl;

    // Dumping the list of command found (if the list is short)
    if (ncommands < 25) {
        cout << endl << "==========" << endl << "**** Dumping the list of commands ...." << endl;
        cout << RCL;
    }
    
    // Print a random command of the list 
    if (ncommands > 0) {
        label:
        srand(rand() + (unsigned long)&&label);
        long r = rand() % ncommands;
        cout << "------------------" << endl;
        cout << endl
             << "Printing one command randomly: the " << r << "-th one out of " << ncommands << ":" << endl;
        // Testing the constructor without arguments
        // it must access the same list, as it is static
        ROAE_command_list RCL2;
        cout << RCL2.command(r) << endl;
        cout << "------------------" << endl;
    }

    // Clear the static list of commands
    RCL.clear();
}

static void test_c(char *roaefile){
    IDA_ROAE_load(roaefile);
    IDA_ROAE_print_commands();
    long ncommands = IDA_ROAE_count(); 
    printf("Found %ld roae commands in file '%s'\n",
            ncommands, roaefile);

    if (ncommands > 0) {
        // Print a random command of the list
        srand(time(NULL) + (unsigned long)test_c);
        long r = rand() % ncommands;
        printf("\n-----------------\n");
        printf("*** Printing one command randomly: the %ld-th one, out of %ld\n", r, ncommands);
        IDA_ROAE_print_command(r);

        char *values[256];
        values[0] = (char*)"ABC";
        values[1] = (char*)"12345";
        values[2] = (char*)"Xyz";
        values[3] = (char*)"WwW";
        values[4] = NULL;
        
        long i=0;
        while (values[i]){
            printf(" - Param. #%ld='%s'\n", i, values[i]);
            i++;
        }

        char request[2048];
        char *ec = IDA_ROAE_eval_command(r, request, sizeof(request), values);
        if (ec) {
            printf("Evaluated SQL: '%s'\n", request);
        } else {
            fprintf(stderr, "Error evaluating '%s' for command number %ld (perhaps command number out of range)", request, r);
        }

        printf("\n-----------------\n");

        // Print a menu to select a rule
        printf("\nMenu:\n");
        for (long i=0; i<ncommands; i++){
            char *c = IDA_ROAE_get_command_title(i);
            printf(" [%02ld] %s\n", i, c);
            free(c);
        };

        #define ROAEBUFFSIZE 2048
        char menubuff[ROAEBUFFSIZE];
        errno=0;
        long nc=0, scnf=0;
        printf("Intro one command: ");
        char *roae = fgets(menubuff, ROAEBUFFSIZE-1, stdin); menubuff[ROAEBUFFSIZE-1]='\0';
        if (roae) scnf = sscanf(roae, "%ld", &nc);
        if (!errno && scnf>0 && nc>=0 && nc<ncommands){
            printf("Selected ROAE command no. %ld\n", nc);
            char *c = IDA_ROAE_get_command_title(nc);
            printf("  title=%s\n", c);
            free(c);
            long npar = IDA_ROAE_get_command_nargs(nc);
            char **arglist;
            arglist = (char**)malloc(sizeof(char*) * (npar+1));
            if (npar > 0) {
                printf("  This rule requires %ld arguments:\n", npar);
                for (long k=0; k<npar; k++){
                    char* arg_name = IDA_ROAE_get_command_arg_name(nc, k);
                    char* arg_comment = IDA_ROAE_get_command_arg_comment(nc, k);
                    printf("   - Intro argument '%s' (%s): ", arg_name, arg_comment);
                    free(arg_name);
                    free(arg_comment);
                    char *arg = fgets(menubuff, ROAEBUFFSIZE-1, stdin);
                    menubuff[ROAEBUFFSIZE-1]='\0';
                    if ('\n' == menubuff[strlen(menubuff)-1]) menubuff[strlen(menubuff)-1] = '\0'; // Remove last newline
                    if (arg)
                        arglist[k] = strdup(arg);
                    else
                        arglist[k] = NULL;
                }
            } else {
                printf("  This rule does not requires any parameter\n");
            }
            arglist[npar] = NULL;

            char **bindarglist = IDA_ROAE_command_bind_list(nc, arglist);
            char* evalcmd = IDA_ROAE_eval_command(nc, NULL, 0, arglist);
            fprintf(stdout, "Evaluated command: '%s'\n", evalcmd);
            if (evalcmd) free(evalcmd);

            char *bindsqlite = IDA_ROAE_command_bind_list_to_sqlite(bindarglist);
            fprintf(stdout, "SQLite bind-command:\n'%s'\n", bindsqlite);
            if (bindsqlite) free(bindsqlite);
            printf("\n-----------------\n");

            #define FREEARGS(args)  do{long i=0; if (args){ while(args[i]){free(args[i]);i++;}; free(args);}}while(0)
            if (arglist) FREEARGS(arglist);
            if (bindarglist) FREEARGS(bindarglist);

        } else {
            fprintf(stderr, "ROAE command number is not a valid integer (0 <= n < %ld)\n", ncommands);
        }
    }
}

void ROAE_test(char *roaefile) {
    char *p0, *p1;
    p0 = (char*)malloc(0);

    // Testing C++ interface
    //test_cpp(roaefile);

    // Testing C interface
    test_c(roaefile);
}
#ifdef __cplusplus
}
#endif
