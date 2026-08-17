#ifndef AOS_RUN_HPP
#define AOS_RUN_HPP

namespace aos {

/*
 * Run the aos-c program: read instructions from the file named by argv[1],
 * or from standard input when no name is given, and execute them in the
 * order they appear.
 *
 * Instructions run sequentially, and a child exiting non-zero does not stop
 * the run -- its status is data, and recording it is what exit_path is for.
 * A failure of the runtime itself does stop the run: a malformed record, or
 * a command that could not be started, leaves the remaining instructions
 * unexecuted rather than continuing past something the caller asked for and
 * did not get.
 *
 * Returns 0 when every instruction ran, and 1 on the first runtime failure.
 */
int run(int argc, char *argv[]);

}  /* namespace aos */

#endif
