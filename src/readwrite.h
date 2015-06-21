// ::TODO LICENSE, SOURCE::

#ifndef READ_WRITE_H
#define READ_WRITE_H

int rd_word_lh(unsigned char *data);
int rd_dword_lh(unsigned char *data);
void wr_word_lh(unsigned char *data,int value);
void wr_dword_lh(unsigned char *data,int value);
int rd_word_hl(unsigned char *data);
int rd_dword_hl(unsigned char *data);
int rd_var_length(unsigned char *data,int &ptr);

#endif

