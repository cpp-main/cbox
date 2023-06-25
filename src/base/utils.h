#ifndef _UTILS_H_20221106_
#define _UTILS_H_20221106_

#if defined (__cplusplus)
extern "C" {
#endif

/*
 * @brief run proc as daemon
 *        e.g.,
 *        if (deamon) {
 *           char dir[256];
 *           readlink("/proc/self/exe", dir, sizeof(dir));
 *           dir[strrchr(dir, '/') - dir] = '\0';
 *           cbox_utils_daemon(dir);
 *        }
 *
 * @param dir: the proc directory
 */
void cbox_utils_daemon(const char *dir);

/*
 * @brief check if process already running
 *
 * @param name: the name of process name
 *
 * @return 1: already running
 * @return 0: not running
 */
int cbox_utils_i_am_running(const char *name);

/*
 * @brief check if current thread is main thread
 *
 * @return 1: already in main
 * @return 0: not running in main
 */
int cbox_utils_running_in_main();


/*
 * @brief convert hex-string to binary array
 *
 * @param bin: output buffer to store binary elements
 * @param hex: input hex-dumped string
 */
void cbox_utils_hexstr_to_bin(const char *hexstr, unsigned char *bin);

/*
 * @brief convert binary array to hexstr
 *
 * @param bin: input binary array
 * @param bin_len: array length
 * @param hexstr: output hexstr
 * @param space: add space between every hexstr, 1:true, 0:false
 * @param upper: make every character upper, 1:true, 0:false
 */
void cbox_utils_bin_to_hexstr(unsigned char *bin, unsigned bin_len, char *hexstr, int space, int upper);


/*
 * @brief excute system command
 *        e.g., cbox_utils_system("ls"); or cbox_utils_system("cat %s", dir);
 *
 * @params fmt: the system command format
 * @note  max len of command must be less than 1024
 */
void cbox_utils_system(const char *fmt, ...);

/*
 * @brief perform command and then get the results with callback function
 *
 * @param cmd: the command you want to excute
 * @param handler: callback to receive all results
 * @param context: user data
 * @param no_lf: if value is 1, replace every end of line '\n' to '\0', value 0 means disable,
 *               you should set it to 1 in most cases
 */
void cbox_utils_perform(const char *cmd, void (*handler)(const char *, const void *), const void *context, int no_lf);

/*
 * @brief copy file from src to dest
 *
 * @return 1: copy succeed
 * @return 0: copy failed
 */
int cbox_utils_copy_file(const char *dest, const char *src);

#if defined (__cplusplus)
}
#endif
#endif
