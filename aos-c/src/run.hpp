#ifndef AOS_RUN_HPP
#define AOS_RUN_HPP

namespace aos {

/*
 * Run the aos-c program: read instructions from the file named by argv[1],
 * or from standard input when no name is given, and execute them in the
 * order they appear.
 *
 * Instructions run sequentially, and neither a child exiting non-zero nor a
 * command that could not be started stops the run -- both are statuses, and
 * recording them is what exit_path is for. A failure of this program itself
 * -- fork, wait, or writing the exit file -- does not stop it either: later
 * instructions do not depend on earlier ones, so abandoning them would turn
 * one failure into many things simply not done.
 *
 * A malformed record does stop the run. The format has no separator between
 * records, so a parse failure means the cursor's position is unknown, and
 * every later record would decode into fields belonging to other records.
 *
 * Returns 0 when every instruction ran, and 1 when any of them failed.
 */
int run(int argc, char *argv[]);

}  /* namespace aos */

#endif
