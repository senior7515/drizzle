/* Copyright (C) 2000-2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* password checking routines */
/*****************************************************************************
  The main idea is that no password are sent between client & server on
  connection and that no password are saved in mysql in a decodable form.

  On connection a random string is generated and sent to the client.
  The client generates a new string with a random generator inited with
  the hash values from the password and the sent string.
  This 'check' string is sent to the server where it is compared with
  a string generated from the stored hash_value of the password and the
  random string.

  The password is saved (in user.password) by using the PASSWORD() function in
  mysql.

  This is .c file because it's used in libmysqlclient, which is entirely in C.
  (we need it to be portable to a variety of systems).
  Example:
    update user set password=PASSWORD("hello") where user="test"
  This saves a hashed number as a string in the password field.

  The new authentication is performed in following manner:

  SERVER:  public_seed=create_random_string()
           send(public_seed)

  CLIENT:  recv(public_seed)
           hash_stage1=sha1("password")
           hash_stage2=sha1(hash_stage1)
           reply=xor(hash_stage1, sha1(public_seed,hash_stage2)

           // this three steps are done in scramble() 

           send(reply)

     
  SERVER:  recv(reply)
           hash_stage1=xor(reply, sha1(public_seed,hash_stage2))
           candidate_hash2=sha1(hash_stage1)
           check(candidate_hash2==hash_stage2)

           // this three steps are done in check_scramble()

*****************************************************************************/

#include <my_global.h>
#include <my_sys.h>
#include <mystrings/m_string.h>
#include <sha1.h>
#include "drizzle.h"

/************ MySQL 3.23-4.0 authentication routines: untouched ***********/

/*
  New (MySQL 3.21+) random generation structure initialization
  SYNOPSIS
    randominit()
    rand_st    OUT  Structure to initialize
    seed1      IN   First initialization parameter
    seed2      IN   Second initialization parameter
*/

void randominit(struct rand_struct *rand_st, uint32_t seed1, uint32_t seed2)
{                                               /* For mysql 3.21.# */
#ifdef HAVE_purify
  bzero((char*) rand_st,sizeof(*rand_st));      /* Avoid UMC varnings */
#endif
  rand_st->max_value= 0x3FFFFFFFL;
  rand_st->max_value_dbl=(double) rand_st->max_value;
  rand_st->seed1=seed1%rand_st->max_value ;
  rand_st->seed2=seed2%rand_st->max_value;
}


/*
    Generate random number.
  SYNOPSIS
    my_rnd()
    rand_st    INOUT  Structure used for number generation
  RETURN VALUE
    generated pseudo random number
*/

double my_rnd(struct rand_struct *rand_st)
{
  rand_st->seed1=(rand_st->seed1*3+rand_st->seed2) % rand_st->max_value;
  rand_st->seed2=(rand_st->seed1+rand_st->seed2+33) % rand_st->max_value;
  return (((double) rand_st->seed1)/rand_st->max_value_dbl);
}


/*
    Generate binary hash from raw text string 
    Used for Pre-4.1 password handling
  SYNOPSIS
    hash_password()
    result       OUT store hash in this location
    password     IN  plain text password to build hash
    password_len IN  password length (password may be not null-terminated)
*/

void hash_password(uint32_t *result, const char *password, uint32_t password_len)
{
  register ulong nr=1345345333L, add=7, nr2=0x12345671L;
  uint32_t tmp;
  const char *password_end= password + password_len;
  for (; password < password_end; password++)
  {
    if (*password == ' ' || *password == '\t')
      continue;                                 /* skip space in password */
    tmp= (uint32_t) (uchar) *password;
    nr^= (((nr & 63)+add)*tmp)+ (nr << 8);
    nr2+=(nr2 << 8) ^ nr;
    add+=tmp;
  }
  result[0]=nr & (((uint32_t) 1L << 31) -1L); /* Don't use sign bit (str2int) */;
  result[1]=nr2 & (((uint32_t) 1L << 31) -1L);
}

static inline uint8_t char_val(uint8_t X)
{
  return (uint) (X >= '0' && X <= '9' ? X-'0' :
      X >= 'A' && X <= 'Z' ? X-'A'+10 : X-'a'+10);
}


/*
     **************** MySQL 4.1.1 authentication routines *************
*/

/*
    Generate string of printable random characters of requested length
  SYNOPSIS
    create_random_string()
    to       OUT   buffer for generation; must be at least length+1 bytes
                   long; result string is always null-terminated
    length   IN    how many random characters to put in buffer
    rand_st  INOUT structure used for number generation
*/

void create_random_string(char *to, uint length, struct rand_struct *rand_st)
{
  char *end= to + length;
  /* Use pointer arithmetics as it is faster way to do so. */
  for (; to < end; to++)
    *to= (char) (my_rnd(rand_st)*94+33);
  *to= '\0';
}


/* Character to use as version identifier for version 4.1 */

#define PVERSION41_CHAR '*'


/*
    Convert given octet sequence to asciiz string of hex characters;
    str..str+len and 'to' may not overlap.
  SYNOPSIS
    octet2hex()
    buf       OUT output buffer. Must be at least 2*len+1 bytes
    str, len  IN  the beginning and the length of the input string

  RETURN
    buf+len*2
*/

char *octet2hex(char *to, const char *str, uint len)
{
  const char *str_end= str + len; 
  for (; str != str_end; ++str)
  {
    *to++= _dig_vec_upper[((uchar) *str) >> 4];
    *to++= _dig_vec_upper[((uchar) *str) & 0x0F];
  }
  *to= '\0';
  return to;
}


/*
    Convert given asciiz string of hex (0..9 a..f) characters to octet
    sequence.
  SYNOPSIS
    hex2octet()
    to        OUT buffer to place result; must be at least len/2 bytes
    str, len  IN  begin, length for character string; str and to may not
                  overlap; len % 2 == 0
*/ 

static void
hex2octet(uint8_t *to, const char *str, uint len)
{
  const char *str_end= str + len;
  while (str < str_end)
  {
    register char tmp= char_val(*str++);
    *to++= (tmp << 4) | char_val(*str++);
  }
}


/*
    Encrypt/Decrypt function used for password encryption in authentication.
    Simple XOR is used here but it is OK as we crypt random strings. Note,
    that XOR(s1, XOR(s1, s2)) == s2, XOR(s1, s2) == XOR(s2, s1)
  SYNOPSIS
    my_crypt()
    to      OUT buffer to hold crypted string; must be at least len bytes
                long; to and s1 (or s2) may be the same.
    s1, s2  IN  input strings (of equal length)
    len     IN  length of s1 and s2
*/

static void
my_crypt(char *to, const uchar *s1, const uchar *s2, uint len)
{
  const uint8_t *s1_end= s1 + len;
  while (s1 < s1_end)
    *to++= *s1++ ^ *s2++;
}


/*
    MySQL 4.1.1 password hashing: SHA conversion (see RFC 2289, 3174) twice
    applied to the password string, and then produced octet sequence is
    converted to hex string.
    The result of this function is used as return value from PASSWORD() and
    is stored in the database.
  SYNOPSIS
    make_scrambled_password()
    buf       OUT buffer of size 2*SHA1_HASH_SIZE + 2 to store hex string
    password  IN  NULL-terminated password string
*/

void
make_scrambled_password(char *to, const char *password)
{
  SHA1_CONTEXT sha1_context;
  uint8_t hash_stage2[SHA1_HASH_SIZE];

  mysql_sha1_reset(&sha1_context);
  /* stage 1: hash password */
  mysql_sha1_input(&sha1_context, (uint8_t *) password, (uint) strlen(password));
  mysql_sha1_result(&sha1_context, (uint8_t *) to);
  /* stage 2: hash stage1 output */
  mysql_sha1_reset(&sha1_context);
  mysql_sha1_input(&sha1_context, (uint8_t *) to, SHA1_HASH_SIZE);
  /* separate buffer is used to pass 'to' in octet2hex */
  mysql_sha1_result(&sha1_context, hash_stage2);
  /* convert hash_stage2 to hex string */
  *to++= PVERSION41_CHAR;
  octet2hex(to, (const char*) hash_stage2, SHA1_HASH_SIZE);
}
  

/*
    Produce an obscure octet sequence from password and random
    string, recieved from the server. This sequence corresponds to the
    password, but password can not be easily restored from it. The sequence
    is then sent to the server for validation. Trailing zero is not stored
    in the buf as it is not needed.
    This function is used by client to create authenticated reply to the
    server's greeting.
  SYNOPSIS
    scramble()
    buf       OUT store scrambled string here. The buf must be at least 
                  SHA1_HASH_SIZE bytes long. 
    message   IN  random message, must be exactly SCRAMBLE_LENGTH long and 
                  NULL-terminated.
    password  IN  users' password 
*/

void
scramble(char *to, const char *message, const char *password)
{
  SHA1_CONTEXT sha1_context;
  uint8_t hash_stage1[SHA1_HASH_SIZE];
  uint8_t hash_stage2[SHA1_HASH_SIZE];

  mysql_sha1_reset(&sha1_context);
  /* stage 1: hash password */
  mysql_sha1_input(&sha1_context, (uint8_t *) password, (uint) strlen(password));
  mysql_sha1_result(&sha1_context, hash_stage1);
  /* stage 2: hash stage 1; note that hash_stage2 is stored in the database */
  mysql_sha1_reset(&sha1_context);
  mysql_sha1_input(&sha1_context, hash_stage1, SHA1_HASH_SIZE);
  mysql_sha1_result(&sha1_context, hash_stage2);
  /* create crypt string as sha1(message, hash_stage2) */;
  mysql_sha1_reset(&sha1_context);
  mysql_sha1_input(&sha1_context, (const uint8_t *) message, SCRAMBLE_LENGTH);
  mysql_sha1_input(&sha1_context, hash_stage2, SHA1_HASH_SIZE);
  /* xor allows 'from' and 'to' overlap: lets take advantage of it */
  mysql_sha1_result(&sha1_context, (uint8_t *) to);
  my_crypt(to, (const uchar *) to, hash_stage1, SCRAMBLE_LENGTH);
}


/*
    Check that scrambled message corresponds to the password; the function
    is used by server to check that recieved reply is authentic.
    This function does not check lengths of given strings: message must be
    null-terminated, reply and hash_stage2 must be at least SHA1_HASH_SIZE
    long (if not, something fishy is going on).
  SYNOPSIS
    check_scramble()
    scramble     clients' reply, presumably produced by scramble()
    message      original random string, previously sent to client
                 (presumably second argument of scramble()), must be 
                 exactly SCRAMBLE_LENGTH long and NULL-terminated.
    hash_stage2  hex2octet-decoded database entry
    All params are IN.

  RETURN VALUE
    0  password is correct
    !0  password is invalid
*/

my_bool
check_scramble(const char *scramble_arg, const char *message,
               const uint8_t *hash_stage2)
{
  SHA1_CONTEXT sha1_context;
  uint8_t buf[SHA1_HASH_SIZE];
  uint8_t hash_stage2_reassured[SHA1_HASH_SIZE];

  mysql_sha1_reset(&sha1_context);
  /* create key to encrypt scramble */
  mysql_sha1_input(&sha1_context, (const uint8_t *) message, SCRAMBLE_LENGTH);
  mysql_sha1_input(&sha1_context, hash_stage2, SHA1_HASH_SIZE);
  mysql_sha1_result(&sha1_context, buf);
  /* encrypt scramble */
    my_crypt((char *) buf, buf, (const uchar *) scramble_arg, SCRAMBLE_LENGTH);
  /* now buf supposedly contains hash_stage1: so we can get hash_stage2 */
  mysql_sha1_reset(&sha1_context);
  mysql_sha1_input(&sha1_context, buf, SHA1_HASH_SIZE);
  mysql_sha1_result(&sha1_context, hash_stage2_reassured);
  return memcmp(hash_stage2, hash_stage2_reassured, SHA1_HASH_SIZE);
}


/*
  Convert scrambled password from asciiz hex string to binary form.

  SYNOPSIS
    get_salt_from_password()
    res       OUT buf to hold password. Must be at least SHA1_HASH_SIZE
                  bytes long.
    password  IN  4.1.1 version value of user.password
*/
    
void get_salt_from_password(uint8_t *hash_stage2, const char *password)
{
  hex2octet(hash_stage2, password+1 /* skip '*' */, SHA1_HASH_SIZE * 2);
}

/*
    Convert scrambled password from binary form to asciiz hex string.
  SYNOPSIS
    make_password_from_salt()
    to    OUT store resulting string here, 2*SHA1_HASH_SIZE+2 bytes 
    salt  IN  password in salt format
*/

void make_password_from_salt(char *to, const uint8_t *hash_stage2)
{
  *to++= PVERSION41_CHAR;
  octet2hex(to, (const char*) hash_stage2, SHA1_HASH_SIZE);
}
