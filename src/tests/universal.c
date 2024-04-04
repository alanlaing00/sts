/*****************************************************************************
			 U N I V E R S A L   T E S T
 *****************************************************************************/

/*
 * This code has been heavily modified by the following people:
 *
 *      Landon Curt Noll
 *      Tom Gilgan
 *      Riccardo Paccagnella
 *
 * See the README.md and the initial comment in sts.c for more information.
 *
 * WE (THOSE LISTED ABOVE WHO HEAVILY MODIFIED THIS CODE) DISCLAIM ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL WE (THOSE LISTED ABOVE
 * WHO HEAVILY MODIFIED THIS CODE) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * chongo (Landon Curt Noll, http://www.isthe.com/chongo/index.html) /\oo/\
 *
 * Share and enjoy! :-)
 */


// Exit codes: 200 thru 209

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "../utils/externs.h"
#include "../utils/utilities.h"
#include "../utils/cephes.h"
#include "../utils/debug.h"


/*
 * Private stats - stats.txt information for this test
 */
struct Universal_private_stats {
	bool success;		// Success or failure of iteration test
	long int Q;		// Number of blocks in the initialization segment
	long int K;		// Number of blocks in the test segment
	double sum;		// Sum of the log2 distances between matching L-bit templates
	double sigma;		// sqrt(variance[L] / K)
	double f_n;		// theoretical standard deviation
};


/*
 * Static const variables declarations
 */
static const enum test test_num = TEST_UNIVERSAL;	// This test number

/*
 * The expected_value[] and variance[] constants are from the following table:
 *
 *      A Handbook of Applied Cryptography, by Alfred J. Menezes, Paul C. van Oorschot, Scott A. Vanstone, 1997
 *      Section 5.4.5 "Maurer's universal statistical test", page 184.
 *
 * The index for these arrays is L.  The expected_value[] array is from the 2nd column (mu),
 * and the variance[] array is from the 3rd column (sigma^2(1)) in the above reference.
 *
 * Because MIN_L_UNIVERSAL is 6, the values for 0 <= L < MIN_L_UNIVERSAL forced to zero.
 */
static const double expected_value[MAX_L_UNIVERSAL + 1] = {
	0, 0, 0, 0, 0, 0, 5.2177052, 6.1962507, 7.1836656,
	8.1764248, 9.1723243, 10.170032, 11.168765,
	12.168070, 13.167693, 14.167488, 15.167379
};
static const double variance[MAX_L_UNIVERSAL + 1] = {
	0, 0, 0, 0, 0, 0, 2.954, 3.125, 3.238, 3.311, 3.356, 3.384,
	3.401, 3.410, 3.416, 3.419, 3.421
};


/*
 * Forward static function declarations
 */
static bool Universal_print_stat(FILE * stream, struct state *state, struct Universal_private_stats *stat, double p_value);
static bool Universal_print_p_value(FILE * stream, double p_value);
static void Universal_metric_print(struct state *state, long int sampleCount, long int toolow, long int *freqPerBin);


/*
 * Universal_init - initialize the Universal test
 *
 * given:
 *      state           // run state to test under
 *
 * This function is called for each and every iteration noted in state->tp.numOfBitStreams.
 *
 * NOTE: The initialize function must be called first.
 */
void
Universal_init(struct state *state)
{
	long int n;		// Length of a single bit stream
	long int L;		// Length of each block
	long int p;		// Number of possible L-bit blocks and size of the table T
	long int i;

	/*
	 * Check preconditions (firewall)
	 */
	if (state == NULL) {
		err(200, __func__, "state arg is NULL");
	}
	if (state->testVector[test_num] != true) {
		dbg(DBG_LOW, "init driver interface for %s[%d] called when test vector was false", state->testNames[test_num],
		    test_num);
		return;
	}
	if (state->cSetup != true) {
		err(200, __func__, "test constants not setup prior to calling %s for %s[%d]",
		    __func__, state->testNames[test_num], test_num);
	}

	/*
	 * Collect parameters from state
	 */
	n = state->tp.n;

	/*
	 * Disable test if conditions do not permit this test from being run
	 */
	if (n < MIN_UNIVERSAL) {
		warn(__func__, "disabling test %s[%d]: requires bitcount(n): %ld >= %d for L >= 6",
		     state->testNames[test_num], test_num, n, MIN_UNIVERSAL);
		state->testVector[test_num] = false;
		return;
	}

	/*
	 * Determine L, the length of each block
	 * Given n, L is the smallest value such that:
	 *      n >= 1010 * 2^L *L
	 */
	for (L = MIN_L_UNIVERSAL + 1; L <= MAX_L_UNIVERSAL; L++) {

		/*
		 * Make sure that L is less than the number of bits in a long, to prevent overflow
		 */
		if (L > (BITS_N_LONGINT - 1)) {
			warn(__func__, "disabling test %s[%d]: L: %ld is too large and should be less than %lu bits long",
			     state->testNames[test_num], test_num, L, BITS_N_LONGINT - 1);
			state->testVector[test_num] = false;
			return;
		}

		/*
		 * Make sure that the operations we are going to perform with L will not cause overflow
		 */
		if (((long int) 1 << L) > ((long int) LONG_MAX / 1010 / L)) {
			warn(__func__, "disabling test %s[%d]: L: %ld is too large, "
					"1010 * (1 << L) * L will overflow long int", state->testNames[test_num], test_num, L);
			state->testVector[test_num] = false;
			return;
		}

		/*
		 * Break if L is too big
		 */
		if (n < (1010 * (1 << L) * L)) {
			break;
		}
	}

	/*
	 * Move back to the L that was not too big
	 */
	L--;

	/*
	 * Check preconditions (firewall)
	 */
	if (L < MIN_L_UNIVERSAL) {
		warn(__func__, "disabling test %s[%d]: L is out of range: %ld < %d",
		     state->testNames[test_num], test_num, L, MIN_L_UNIVERSAL);
		state->testVector[test_num] = false;
		return;
	} else if (L > MAX_L_UNIVERSAL) {
		warn(__func__, "disabling test %s[%d]: L is out of range: %ld > %d",
		     state->testNames[test_num], test_num, L, MAX_L_UNIVERSAL);
		state->testVector[test_num] = false;
		return;
	}

	/*
	 * Store computed L into state
	 */
	state->universal_L = L;
	p = (long int) 1 << L;

	/*
	 * Allocate the T table (with block number of the last occurrence of each block) for each thread
	 */
	state->universal_T = malloc((size_t) state->numberOfThreads * sizeof(*state->universal_T));
	if (state->universal_T == NULL) {
		errp(200, __func__, "cannot malloc for universal_T: %ld elements of %lu bytes each", state->numberOfThreads,
		     sizeof(*state->universal_T));
	}
	for (i = 0; i < state->numberOfThreads; i++) {
		state->universal_T[i] = malloc(p * sizeof(state->universal_T[i][0]));
		if (state->universal_T[i] == NULL) {
			errp(200, __func__, "cannot malloc of %ld elements of %ld bytes each for state->universal_T[%ld]",
			     p, sizeof(state->universal_T[i][0]), i);
		}
	}

	/*
	 * Create working sub-directory if forming files such as results.txt and stats.txt
	 */
	if (state->resultstxtFlag == true) {
		state->subDir[test_num] = precheckSubdir(state, state->testNames[test_num]);
		dbg(DBG_HIGH, "test %s[%d] will use subdir: %s", state->testNames[test_num], test_num, state->subDir[test_num]);
	}

	/*
	 * Allocate dynamic arrays
	 */
	if (state->resultstxtFlag == true) {
		state->stats[test_num] = create_dyn_array(sizeof(struct Universal_private_stats),
							  DEFAULT_CHUNK, state->tp.numOfBitStreams, false);        // stats.txt
	}
	state->p_val[test_num] = create_dyn_array(sizeof(double),
						  DEFAULT_CHUNK, state->tp.numOfBitStreams, false);	// results.txt

	/*
	 * Determine format of data*.txt filenames based on state->partitionCount[test_num]
	 * NOTE: If we are not partitioning the p_values, no data*.txt filenames are needed
	 */
	state->datatxt_fmt[test_num] = data_filename_format(state->partitionCount[test_num]);
	dbg(DBG_HIGH, "%s[%d] will form data*.txt filenames with the following format: %s",
	    state->testNames[test_num], test_num, state->datatxt_fmt[test_num]);

	return;
}


/*
 * Universal_iterate - iterate one bit stream for Universal test
 *
 * given:
 *      state           // run state to test under
 *
 * This function is called for each and every iteration noted in state->tp.numOfBitStreams.
 *
 * NOTE: The initialize function must be called first.
 */
void
Universal_iterate(struct thread_state *thread_state)
{
	struct Universal_private_stats stat;	// Stats for this iteration
	long int L;		// Length of each block
	long int *T;		// Table with block number of the last occurrence of each block
	long int p;		// Number of possible L-bit blocks and size of the table T
	double arg;		// Term used to compute p-value
	double p_value;		// p_value iteration test result(s)
	double c;		// Constant used in the formula of the standard deviation
	long decRep;		// Decimal representation of a block
	long int i;
	long int j;

	/*
	 * Check preconditions (firewall)
	 */
	if (thread_state == NULL) {
		err(201, __func__, "thread_state arg is NULL");
	}
	struct state *state = thread_state->global_state;
	if (state == NULL) {
		err(201, __func__, "state arg is NULL");
	}
	if (state->testVector[test_num] != true) {
		dbg(DBG_LOW, "iterate function[%d] %s called when test vector was false", test_num, __func__);
		return;
	}
	if (state->epsilon == NULL) {
		err(201, __func__, "state->epsilon is NULL");
	}
	if (state->epsilon[thread_state->thread_id] == NULL) {
		err(201, __func__, "state->epsilon[%ld] is NULL", thread_state->thread_id);
	}
	if (state->universal_T == NULL) {
		err(201, __func__, "state->universal_T is NULL");
	}
	if (state->universal_T[thread_state->thread_id] == NULL) {
		err(201, __func__, "state->universal_T[%ld] is NULL", thread_state->thread_id);
	}
	if (state->cSetup != true) {
		err(201, __func__, "test constants not setup prior to calling %s for %s[%d]",
		    __func__, state->testNames[test_num], test_num);
	}

	/*
	 * Collect parameters from state
	 */
	L = state->universal_L;
	T = state->universal_T[thread_state->thread_id];

	/*
	 * Check preconditions (firewall)
	 */
	if (L < MIN_L_UNIVERSAL) {
		err(201, __func__, "L is out of range: %ld < %d", L, MIN_L_UNIVERSAL);
	} else if (L > MAX_L_UNIVERSAL) {
		err(201, __func__, "L is out of range: %ld > %d", L, MAX_L_UNIVERSAL);
	}

	/*
	 * Setup parameters for the test
	 */
	p = (long int) 1 << L;
	if (p > ((long int) LONG_MAX / 10)) {	// paranoia
		err(201, __func__, "L: %ld is too large, 10 * 1 << L will overflow long int", L);
	}
	stat.Q = 10 * p;
	stat.K = 100 * stat.Q;
	stat.sum = 0.0;
	memset(T, 0, p * sizeof(T[0]));	// zeroize T

	/*
	 * Step 2: using the initialization segment, fill table T with block number
	 * of the last occurrence of each L-bit block.
	 */
	for (i = 1; i <= stat.Q; i++) {

		/*
		 * Get decimal representation of the block.
		 * It is convenient to use this representation because we can store and
		 * have access to the contents of each block in the table T with size 2^L.
		 */
		decRep = 0;
		for (j = 0; j < L; j++) {
			decRep += state->epsilon[thread_state->thread_id][(i - 1) * L + j] * ((long int) 1 << (L - 1 - j));
		}

		/*
		 * Save the block number of this last occurrence of the this L-bit block in the table.
		 */
		T[decRep] = i;
	}

	/*
	 * Step 3: examine each of the K blocks in the test segment and determine
	 * the number of blocks since the last occurrence of the same L-bit block
	 */
	for (i = stat.Q + 1; i <= stat.Q + stat.K; i++) {

		/*
		 * Get decimal representation of the block
		 */
		decRep = 0;
		for (j = 0; j < L; j++) {
			decRep += state->epsilon[thread_state->thread_id][(i - 1) * L + j] * ((long int) 1 << (L - 1 - j));
		}

		/*
		 * Add the distance between re-occurrences of the same L-bit block to an
		 * accumulating log2 sum of all the differences detected in the K blocks
		 */
		stat.sum += log(i - T[decRep]) / state->c.log2;

		/*
		 * Replace the value in the table with the location of the current block
		 */
		T[decRep] = i;
	}

	/*
	 * Step 4: compute the test statistic
	 */
	stat.f_n = (stat.sum / (double) stat.K);

	/*
	 * Step 5: compute the test p-value
	 */
	c = 0.7 - 0.8 / (double) L + (4 + 32 / (double) L) * pow(stat.K, -3.0 / (double) L) / 15;
	stat.sigma = c * sqrt(variance[L] / (double) stat.K);
	arg = fabs(stat.f_n - expected_value[L]) / (state->c.sqrt2 * stat.sigma);
	p_value = erfc(arg);

	/*
	 * Lock mutex before making changes to the shared state
	 */
	if (thread_state->mutex != NULL) {
		pthread_mutex_lock(thread_state->mutex);
	}

	/*
	 * Record success or failure for this iteration
	 */
	state->count[test_num]++;	// Count this iteration
	state->valid[test_num]++;	// Count this valid iteration
	if (isNegative(p_value)) {
		state->failure[test_num]++;	// Bogus p_value < 0.0 treated as a failure
		stat.success = false;		// FAILURE
		warn(__func__, "iteration %ld of test %s[%d] produced bogus p_value: %f < 0.0\n",
		     thread_state->iteration_being_done + 1, state->testNames[test_num], test_num, p_value);
	} else if (isGreaterThanOne(p_value)) {
		state->failure[test_num]++;	// Bogus p_value > 1.0 treated as a failure
		stat.success = false;		// FAILURE
		warn(__func__, "iteration %ld of test %s[%d] produced bogus p_value: %f > 1.0\n",
		     thread_state->iteration_being_done + 1, state->testNames[test_num], test_num, p_value);
	} else if (p_value < state->tp.alpha) {
		state->valid_p_val[test_num]++;	// Valid p_value in [0.0, 1.0] range
		state->failure[test_num]++;	// Valid p_value but too low is a failure
		stat.success = false;		// FAILURE
	} else {
		state->valid_p_val[test_num]++;	// Valid p_value in [0.0, 1.0] range
		state->success[test_num]++;	// Valid p_value not too low is a success
		stat.success = true;		// SUCCESS
	}

	/*
	 * Record values computed during this iteration
	 */
	if (state->resultstxtFlag == true) {
		append_value(state->stats[test_num], &stat);
	}
	append_value(state->p_val[test_num], &p_value);

	/*
	 * Unlock mutex after making changes to the shared state
	 */
	if (thread_state->mutex != NULL) {
		pthread_mutex_unlock(thread_state->mutex);
	}

	return;
}


/*
 * Universal_print_stat - print private_stats information to the end of an open file
 *
 * given:
 *      stream          // open writable FILE stream
 *      state           // run state to test under
 *      stat            // struct Universal_private_stats for format and print
 *      p_value         // p_value iteration test result(s)
 *
 * returns:
 *      true --> no errors
 *      false --> an I/O error occurred
 */
static bool
Universal_print_stat(FILE * stream, struct state *state, struct Universal_private_stats *stat, double p_value)
{
	long int n;		// Length of a single bit stream
	long int L;		// Length of each block
	int io_ret;		// I/O return status

	/*
	 * Check preconditions (firewall)
	 */
	if (stream == NULL) {
		err(202, __func__, "stream arg is NULL");
	}
	if (state == NULL) {
		err(202, __func__, "state arg is NULL");
	}
	if (stat == NULL) {
		err(202, __func__, "stat arg is NULL");
	}
	if (p_value == NON_P_VALUE && stat->success == true) {
		err(202, __func__, "p_value was set to NON_P_VALUE but stat->success == true");
	}

	/*
	 * Collect parameters from state
	 */
	n = state->tp.n;
	L = state->universal_L;

	/*
	 * Print stat to a file
	 */
	if (state->legacy_output == true) {
		io_ret = fprintf(stream, "\t\tUNIVERSAL STATISTICAL TEST\n");
		if (io_ret <= 0) {
			return false;
		}
		io_ret = fprintf(stream, "\t\t--------------------------------------------\n");
		if (io_ret <= 0) {
			return false;
		}
		io_ret = fprintf(stream, "\t\tCOMPUTATIONAL INFORMATION:\n");
		if (io_ret <= 0) {
			return false;
		}
	} else {
		io_ret = fprintf(stream, "\t\tUniversal statistical test\n");
		if (io_ret <= 0) {
			return false;
		}
	}
	io_ret = fprintf(stream, "\t\t--------------------------------------------\n");
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(a) L         = %ld\n", L);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(b) Q         = %ld\n", stat->Q);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(c) K         = %ld\n", stat->K);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(d) sum       = %f\n", stat->sum);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(e) sigma     = %f\n", stat->sigma);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(f) variance  = %f\n", variance[L]);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(g) exp_value = %f\n", expected_value[L]);
	if (io_ret <= 0) {
		return false;
	}
	io_ret = fprintf(stream, "\t\t(h) phi       = %f\n", stat->f_n);
	if (io_ret <= 0) {
		return false;
	}
	if (state->legacy_output == true) {
		io_ret = fprintf(stream, "\t\t(i) WARNING:  %ld bits were discarded.\n", n - (stat->Q + stat->K) * L);
		if (io_ret <= 0) {
			return false;
		}
	} else {
		io_ret = fprintf(stream, "\t\t(i) discarded = %ld\n", n - (stat->Q + stat->K) * L);
		if (io_ret <= 0) {
			return false;
		}
	}
	io_ret = fprintf(stream, "\t\t-----------------------------------------\n");
	if (io_ret <= 0) {
		return false;
	}
	if (stat->success == true) {
		io_ret = fprintf(stream, "SUCCESS\t\tp_value = %f\n\n", p_value);
		if (io_ret <= 0) {
			return false;
		}
	} else if (p_value == NON_P_VALUE) {
		io_ret = fprintf(stream, "FAILURE\t\tp_value = __INVALID__\n\n");
		if (io_ret <= 0) {
			return false;
		}
	} else {
		io_ret = fprintf(stream, "FAILURE\t\tp_value = %f\n\n", p_value);
		if (io_ret <= 0) {
			return false;
		}
	}

	/*
	 * All printing successful
	 */
	return true;
}


/*
 * Universal_print_p_value - print p_value information to the end of an open file
 *
 * given:
 *      stream          // open writable FILE stream
 *      stat            // struct Universal_private_stats for format and print
 *      p_value         // p_value iteration test result(s)
 *
 * returns:
 *      true --> no errors
 *      false --> an I/O error occurred
 */
static bool
Universal_print_p_value(FILE * stream, double p_value)
{
	int io_ret;		// I/O return status

	/*
	 * Check preconditions (firewall)
	 */
	if (stream == NULL) {
		err(203, __func__, "stream arg is NULL");
	}

	/*
	 * Print p_value to a file
	 */
	if (p_value == NON_P_VALUE) {
		io_ret = fprintf(stream, "__INVALID__\n");
		if (io_ret <= 0) {
			return false;
		}
	} else {
		io_ret = fprintf(stream, "%f\n", p_value);
		if (io_ret <= 0) {
			return false;
		}
	}

	/*
	 * All printing successful
	 */
	return true;
}


/*
 * Universal_print - print to results.txt, data*.txt, stats.txt for all iterations
 *
 * given:
 *      state           // run state to test under
 *
 * This function is called for once to print dynamic arrays into
 * results.txt, data*.txt, stats.txt.
 *
 * NOTE: The initialize and iterate functions must be called before this function is called.
 */
void
Universal_print(struct state *state)
{
	struct Universal_private_stats *stat;	// Pointer to statistics of an iteration
	double p_value;			// p_value iteration test result(s)
	FILE *stats = NULL;		// Open stats.txt file
	FILE *results = NULL;		// Open results.txt file
	FILE *data = NULL;		// Open data*.txt file
	char *stats_txt = NULL;		// Pathname for stats.txt
	char *results_txt = NULL;	// Pathname for results.txt
	char *data_txt = NULL;		// Pathname for data*.txt
	char data_filename[BUFSIZ + 1];	// Basename for a given data*.txt pathname
	bool ok;			// true -> I/O was OK
	int snprintf_ret;		// snprintf return value
	int io_ret;			// I/O return status
	long int i;
	long int j;

	/*
	 * Check preconditions (firewall)
	 */
	if (state == NULL) {
		err(204, __func__, "state arg is NULL");
	}
	if (state->testVector[test_num] != true) {
		dbg(DBG_HIGH, "Print driver interface for %s[%d] called when test vector was false", state->testNames[test_num],
		    test_num);
		return;
	}
	if (state->resultstxtFlag == false) {
		dbg(DBG_HIGH, "Print driver interface for %s[%d] was not enabled with -s", state->testNames[test_num], test_num);
		return;
	}
	if (state->partitionCount[test_num] < 1) {
		err(204, __func__,
		    "print driver interface for %s[%d] called with state.partitionCount: %d < 0",
		    state->testNames[test_num], test_num, state->partitionCount[test_num]);
	}
	if (state->p_val[test_num]->count != (state->tp.numOfBitStreams * state->partitionCount[test_num])) {
		err(204, __func__,
		    "print driver interface for %s[%d] called with p_val count: %ld != %ld*%d=%ld",
		    state->testNames[test_num], test_num, state->p_val[test_num]->count,
		    state->tp.numOfBitStreams, state->partitionCount[test_num],
		    state->tp.numOfBitStreams * state->partitionCount[test_num]);
	}
	if (state->datatxt_fmt[test_num] == NULL) {
		err(204, __func__, "format for data0*.txt filename is NULL");
	}

	/*
	 * Open stats.txt file
	 */
	stats_txt = filePathName(state->subDir[test_num], "stats.txt");
	dbg(DBG_HIGH, "about to open/truncate: %s", stats_txt);
	stats = openTruncate(stats_txt);

	/*
	 * Open results.txt file
	 */
	results_txt = filePathName(state->subDir[test_num], "results.txt");
	dbg(DBG_HIGH, "about to open/truncate: %s", results_txt);
	results = openTruncate(results_txt);

	/*
	 * Write results.txt and stats.txt files
	 */
	for (i = 0; i < state->stats[test_num]->count; ++i) {

		/*
		 * Locate stat for this iteration
		 */
		stat = addr_value(state->stats[test_num], struct Universal_private_stats, i);

		/*
		 * Get p_value for this iteration
		 */
		p_value = get_value(state->p_val[test_num], double, i);

		/*
		 * Print stat to stats.txt
		 */
		errno = 0;	// paranoia
		ok = Universal_print_stat(stats, state, stat, p_value);
		if (ok == false) {
			errp(204, __func__, "error in writing to %s", stats_txt);
		}

		/*
		 * Print p_value to results.txt
		 */
		errno = 0;	// paranoia
		ok = Universal_print_p_value(results, p_value);
		if (ok == false) {
			errp(204, __func__, "error in writing to %s", results_txt);
		}
	}

	/*
	 * Flush and close stats.txt, free pathname
	 */
	errno = 0;		// paranoia
	io_ret = fflush(stats);
	if (io_ret != 0) {
		errp(204, __func__, "error flushing to: %s", stats_txt);
	}
	errno = 0;		// paranoia
	io_ret = fclose(stats);
	if (io_ret != 0) {
		errp(204, __func__, "error closing: %s", stats_txt);
	}
	free(stats_txt);
	stats_txt = NULL;

	/*
	 * Flush and close results.txt, free pathname
	 */
	errno = 0;		// paranoia
	io_ret = fflush(results);
	if (io_ret != 0) {
		errp(204, __func__, "error flushing to: %s", results_txt);
	}
	errno = 0;		// paranoia
	io_ret = fclose(results);
	if (io_ret != 0) {
		errp(204, __func__, "error closing: %s", results_txt);
	}
	free(results_txt);
	results_txt = NULL;

	/*
	 * Write data*.txt for each data file if we need to partition results
	 */
	if (state->partitionCount[test_num] > 1) {

		/*
		 * For each data file
		 */
		for (j = 0; j < state->partitionCount[test_num]; ++j) {

			/*
			 * Form the data*.txt basename
			 */
			errno = 0;	// paranoia
			snprintf_ret = snprintf(data_filename, BUFSIZ, state->datatxt_fmt[test_num], j + 1);
			data_filename[BUFSIZ] = '\0';	// paranoia
			if (snprintf_ret <= 0 || snprintf_ret >= BUFSIZ || errno != 0) {
				errp(204, __func__,
				     "snprintf failed for %d bytes for data%03ld.txt, returned: %d", BUFSIZ, j + 1, snprintf_ret);
			}

			/*
			 * Form the data*.txt filename
			 */
			data_txt = filePathName(state->subDir[test_num], data_filename);
			dbg(DBG_HIGH, "about to open/truncate: %s", data_txt);
			data = openTruncate(data_txt);

			/*
			 * Write this particular data*.txt filename
			 */
			if (j < state->p_val[test_num]->count) {
				for (i = j; i < state->p_val[test_num]->count; i += state->partitionCount[test_num]) {

					/*
					 * Get p_value for an iteration belonging to this data*.txt filename
					 */
					p_value = get_value(state->p_val[test_num], double, i);

					/*
					 * Print p_value to results.txt
					 */
					errno = 0;	// paranoia
					ok = Universal_print_p_value(data, p_value);
					if (ok == false) {
						errp(204, __func__, "error in writing to %s", data_txt);
					}

				}
			}

			/*
			 * Flush and close data*.txt, free pathname
			 */
			errno = 0;	// paranoia
			io_ret = fflush(data);
			if (io_ret != 0) {
				errp(204, __func__, "error flushing to: %s", data_txt);
			}
			errno = 0;	// paranoia
			io_ret = fclose(data);
			if (io_ret != 0) {
				errp(204, __func__, "error closing: %s", data_txt);
			}
			free(data_txt);
			data_txt = NULL;
		}
	}

	return;
}


/*
 * Universal_metric_print - print uniformity and proportional information for a tallied count
 *
 * given:
 *      state           	// run state to test under
 *      sampleCount             // number of bitstreams in which we counted p_values
 *      toolow                  // p_values that were below alpha
 *      freqPerBin              // uniformity frequency bins
 */
static void
Universal_metric_print(struct state *state, long int sampleCount, long int toolow, long int *freqPerBin)
{
	long int passCount;	// p_values that pass
	double p_hat;		// 1 - alpha
	double proportion_threshold_max;	// When passCount is too high
	double proportion_threshold_min;	// When passCount is too low
	double chi2;		// Sum of chi^2 for each tenth
	double uniformity;	// Uniformity of frequency bins
	double expCount;	// Sample size divided by frequency bin count
	int io_ret;		// I/O return status
	long int i;

	/*
	 * Check preconditions (firewall)
	 */
	if (state == NULL) {
		err(205, __func__, "state arg is NULL");
	}
	if (freqPerBin == NULL) {
		err(205, __func__, "freqPerBin arg is NULL");
	}

	/*
	 * Determine the number tests that passed
	 */
	if ((sampleCount <= 0) || (sampleCount < toolow)) {
		passCount = 0;
	} else {
		passCount = sampleCount - toolow;
	}

	/*
	 * Determine proportion thresholds
	 */
	p_hat = 1.0 - state->tp.alpha;
	proportion_threshold_max = (p_hat + 3.0 * sqrt((p_hat * state->tp.alpha) / sampleCount)) * sampleCount;
	proportion_threshold_min = (p_hat - 3.0 * sqrt((p_hat * state->tp.alpha) / sampleCount)) * sampleCount;

	/*
	 * Compute uniformity p-value
	 */
	chi2 = 0.0;
	expCount = (double)sampleCount / state->tp.uniformity_bins;
	if (expCount <= 0.0) {
		// Not enough samples for uniformity check
		uniformity = 0.0;
	} else {
		// Sum chi squared of the frequency bins
		for (i = 0; i < state->tp.uniformity_bins; ++i) {
			chi2 += (freqPerBin[i] - expCount) * (freqPerBin[i] - expCount) / expCount;
		}
		// Uniformity threshold level
		uniformity = cephes_igamc((state->tp.uniformity_bins - 1.0) / 2.0, chi2 / 2.0);
	}

	/*
	 * Save or print results
	 */
	if (state->legacy_output == true) {

		/*
		 * Output uniformity results in traditional format to finalAnalysisReport.txt
		 */
		for (i = 0; i < state->tp.uniformity_bins; ++i) {
			fprintf(state->finalRept, "%3ld ", freqPerBin[i]);
		}
		if (expCount <= 0.0) {
			// Not enough samples for uniformity check
			fprintf(state->finalRept, "    ----    ");
			dbg(DBG_HIGH, "too few iterations for uniformity check on %s", state->testNames[test_num]);
		} else if (uniformity < state->tp.uniformity_level) {
			// Uniformity failure (the uniformity p-value is smaller than the minimum uniformity_level (default 0.0001)
			fprintf(state->finalRept, " %8.6f * ", uniformity);
			dbg(DBG_HIGH, "metrics detected uniformity failure for %s", state->testNames[test_num]);
		} else {
			// Uniformity success
			fprintf(state->finalRept, " %8.6f   ", uniformity);
			dbg(DBG_HIGH, "metrics detected uniformity success for %s", state->testNames[test_num]);
		}

		/*
		 * Output proportional results in traditional format to finalAnalysisReport.txt
		 */
		if (sampleCount == 0) {
			// Not enough samples for proportional check
			fprintf(state->finalRept, " ------     %s\n", state->testNames[test_num]);
			dbg(DBG_HIGH, "too few samples for proportional check on %s", state->testNames[test_num]);
		} else if ((passCount < proportion_threshold_min) || (passCount > proportion_threshold_max)) {
			// Proportional failure
			fprintf(state->finalRept, "%4ld/%-4ld *	 %s\n", passCount, sampleCount, state->testNames[test_num]);
			dbg(DBG_HIGH, "metrics detected proportional failure for %s", state->testNames[test_num]);
		} else {
			// Proportional success
			fprintf(state->finalRept, "%4ld/%-4ld	 %s\n", passCount, sampleCount, state->testNames[test_num]);
			dbg(DBG_HIGH, "metrics detected proportional success for %s", state->testNames[test_num]);
		}

		/*
		 * Flush the output file buffer
		 */
		errno = 0;                // paranoia
		io_ret = fflush(state->finalRept);
		if (io_ret != 0) {
			errp(205, __func__, "error flushing to: %s", state->finalReptPath);
		}

	} else {
		bool uniformity_passed = true;
		bool proportion_passed = true;

		/*
		 * Check uniformity results
		 */
		if (expCount <= 0.0 || uniformity < state->tp.uniformity_level) {
			// Uniformity failure or not enough samples for uniformity check
			uniformity_passed = false;
			dbg(DBG_HIGH, "metrics detected uniformity failure for %s", state->testNames[test_num]);
		}

		/*
		 * Check proportional results
		 */
		if (sampleCount == 0 || (passCount < proportion_threshold_min) || (passCount > proportion_threshold_max)) {
			// Proportional failure or not enough samples for proportional check
			proportion_passed = false;
			dbg(DBG_HIGH, "metrics detected proportional failure for %s", state->testNames[test_num]);
		}

		if (proportion_passed == false && uniformity_passed == false) {
			state->metric_results.universal = FAILED_BOTH;
		} else if (proportion_passed == false) {
			state->metric_results.universal = FAILED_PROPORTION;
		} else if (uniformity_passed == false) {
			state->metric_results.universal = FAILED_UNIFORMITY;
		} else {
			state->metric_results.universal = PASSED_BOTH;
			state->successful_tests++;
		}
	}

	return;
}


/*
 * Universal_metrics - uniformity and proportional analysis of a test
 *
 * given:
 *      state           // run state to test under
 *
 * This function is called once to complete the test analysis for all iterations.
 *
 * NOTE: The initialize and iterate functions must be called before this function is called.
 */
void
Universal_metrics(struct state *state)
{
	long int sampleCount;	// Number of bitstreams in which we will count p_values
	long int toolow;	// p_values that were below alpha
	double p_value;		// p_value iteration test result(s)
	long int *freqPerBin;	// Uniformity frequency bins
	long int i;
	long int j;

	/*
	 * Check preconditions (firewall)
	 */
	if (state == NULL) {
		err(206, __func__, "state arg is NULL");
	}
	if (state->testVector[test_num] != true) {
		dbg(DBG_LOW, "metrics driver interface for %s[%d] called when test vector was false", state->testNames[test_num],
		    test_num);
		return;
	}
	if (state->partitionCount[test_num] < 1) {
		err(206, __func__,
		    "metrics driver interface for %s[%d] called with state.partitionCount: %d < 0",
		    state->testNames[test_num], test_num, state->partitionCount[test_num]);
	}
	if (state->p_val[test_num]->count != (state->tp.numOfBitStreams * state->partitionCount[test_num])) {
		warn(__func__,
		     "metrics driver interface for %s[%d] called with p_val length: %ld != bit streams: %ld",
		     state->testNames[test_num], test_num, state->p_val[test_num]->count,
		     state->tp.numOfBitStreams * state->partitionCount[test_num]);
	}

	/*
	 * Allocate uniformity frequency bins
	 */
	freqPerBin = malloc(state->tp.uniformity_bins * sizeof(freqPerBin[0]));
	if (freqPerBin == NULL) {
		errp(206, __func__, "cannot malloc of %ld elements of %ld bytes each for freqPerBin",
		     state->tp.uniformity_bins, sizeof(long int));
	}

	/*
	 * Print for each partition (or the whole set of p_values if partitionCount is 1)
	 */
	for (j = 0; j < state->partitionCount[test_num]; ++j) {

		/*
		 * Set counters to zero
		 */
		toolow = 0;
		sampleCount = 0;
		memset(freqPerBin, 0, state->tp.uniformity_bins * sizeof(freqPerBin[0]));

		/*
		 * Tally p_value
		 */
		for (i = j; i < state->p_val[test_num]->count; i += state->partitionCount[test_num]) {

			// Get the iteration p_value
			p_value = get_value(state->p_val[test_num], double, i);
			if (p_value == NON_P_VALUE) {
				continue;	// the test was not possible for this iteration
			}
			// Case: random excursion test
			if (state->is_excursion[test_num] == true) {
				// Random excursion tests only sample > 0 p_values
				if (p_value > 0.0) {
					++sampleCount;
				} else {
					// Ignore p_value of 0 for random excursion tests
					continue;
				}

				// Case: general (non-random excursion) test
			} else {
				// All other tests count all p_values
				++sampleCount;
			}

			// Count the number of p_values below alpha
			if (p_value < state->tp.alpha) {
				++toolow;
			}
			// Tally the p_value in a uniformity bin
			if (p_value >= 1.0) {
				++freqPerBin[state->tp.uniformity_bins - 1];
			} else if (p_value >= 0.0) {
				++freqPerBin[(int) floor(p_value * (double) state->tp.uniformity_bins)];
			} else {
				++freqPerBin[0];
			}
		}

		/*
		 * Print uniformity and proportional information for a tallied count
		 */
		Universal_metric_print(state, sampleCount, toolow, freqPerBin);

		/*
		 * Track maximum samples
		 */
		if (state->is_excursion[test_num] == true) {
			if (sampleCount > state->maxRandomExcursionSampleSize) {
				state->maxRandomExcursionSampleSize = sampleCount;
			}
		} else {
			if (sampleCount > state->maxGeneralSampleSize) {
				state->maxGeneralSampleSize = sampleCount;
			}
		}
	}

	/*
	 * Free allocated storage
	 */
	free(freqPerBin);
	freqPerBin = NULL;

	return;
}


/*
 * Universal_destroy - post process results for this text
 *
 * given:
 *      state           // run state to test under
 *
 * This function is called once to cleanup any storage or state
 * associated with this test.
 */
void
Universal_destroy(struct state *state)
{
	long int i;

	/*
	 * Check preconditions (firewall)
	 */
	if (state == NULL) {
		err(207, __func__, "state arg is NULL");
	}
	if (state->testVector[test_num] != true) {
		dbg(DBG_LOW, "destroy function[%d] %s called when test vector was false", test_num, __func__);
		return;
	}

	/*
	 * Free dynamic arrays
	 */
	if (state->stats[test_num] != NULL) {
		free_dyn_array(state->stats[test_num]);
		free(state->stats[test_num]);
		state->stats[test_num] = NULL;
	}
	if (state->p_val[test_num] != NULL) {
		free_dyn_array(state->p_val[test_num]);
		free(state->p_val[test_num]);
		state->p_val[test_num] = NULL;
	}

	/*
	 * Free other test storage
	 */
	if (state->datatxt_fmt[test_num] != NULL) {
		free(state->datatxt_fmt[test_num]);
		state->datatxt_fmt[test_num] = NULL;
	}
	if (state->subDir[test_num] != NULL) {
		free(state->subDir[test_num]);
		state->subDir[test_num] = NULL;
	}
	for (i = 0; i < state->numberOfThreads; i++) {
		if (state->universal_T[i] != NULL) {
			free(state->universal_T[i]);
			state->universal_T[i] = NULL;
		}
	}
	if (state->universal_T != NULL) {
		free(state->universal_T);
		state->universal_T = NULL;
	}

	return;
}
