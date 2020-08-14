/* coroutine.h
 * Modified to place state in class member variables. Documentation may be inaccurate.
 *
 * Coroutine mechanics, implemented on top of standard ANSI C. See
 * https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html for
 * a full discussion of the theory behind this.
 *
 * To use these macros to define a coroutine, you need to write a
 * function that looks something like this.
 *
 * [Simple version using static variables (scr macros)]
 * int ascending (void) {
 *    static int i;
 *
 *    scrBegin;
 *    for (i=0; i<10; i++) {
 *       scrReturn(i);
 *    }
 *    scrFinish(-1);
 * }
 *
 * In the static version, you need only surround the function body
 * with `scrBegin' and `scrFinish', and then you can do `scrReturn'
 * within the function and on the next call control will resume
 * just after the scrReturn statement. Any local variables you need
 * to be persistent across an `scrReturn' must be declared static.
 *
 * A coroutine returning void type may call `ccrReturnV',
 * `ccrFinishV' and `ccrStopV', or `scrReturnV', to avoid having to
 * specify an empty parameter to the ordinary return macros.
 *
 * Ground rules:
 *  - never put `ccrReturn' or `scrReturn' within an explicit `switch'.
 *  - never put two `ccrReturn' or `scrReturn' statements on the same
 *    source line.
 *
 * The caller of a static coroutine calls it just as if it were an
 * ordinary function:
 *
 * void main(void) {
 *    int i;
 *    do {
 *       i = ascending();
 *       printf("got number %d\n", i);
 *    } while (i != -1);
 * }
 *
 * This mechanism could have been better implemented using GNU C
 * and its ability to store pointers to labels, but sadly this is
 * not part of the ANSI C standard and so the mechanism is done by
 * case statements instead. That's why you can't put a crReturn()
 * inside a switch() statement.
 */

/*
 * coroutine.h is copyright 1995,2000 Simon Tatham.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#ifndef COROUTINE_H
#define COROUTINE_H

/*
 * `scr' macros for class-member coroutines.
 */

#define scrDefine        int scrLine = 0
#define scrBegin         switch(scrLine) { case 0:;
#define scrFinish(z)     } return (z)
#define scrFinishV       } return
#define scrFinishUnreachable  }

#define scrReturn(z)     \
        do {\
            scrLine=__LINE__;\
            return (z); case __LINE__:;\
        } while (0)
#define scrReturnV       \
        do {\
            scrLine=__LINE__;\
            return; case __LINE__:;\
        } while (0)

#define scrBeginScope  {

/// This is such a cursed macro.
/// Using this macro will unconditionally exit the current scope,
/// breaking out immediately if it's a loop.
/// To avoid breaking from a loop, you need to use scrBeginScope to define
/// an extra nesting level, where jumping out doesn't break from the loop.
#define scrReturnEndScope(z)     \
            scrLine=__LINE__;\
            return (z);\
        }\
        case __LINE__:;


#endif /* COROUTINE_H */
