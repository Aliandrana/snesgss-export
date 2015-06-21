
int rd_word_lh(unsigned char *data)
{
	return data[0]+(data[1]<<8);
}



int rd_dword_lh(unsigned char *data)
{
	return data[0]+(data[1]<<8)+(data[2]<<16)+(data[3]<<24);
}



void wr_word_lh(unsigned char *data,int value)
{
	data[0]=value&255;
	data[1]=(value>>8)&255;
}



void wr_dword_lh(unsigned char *data,int value)
{
	data[0]=value&255;
	data[1]=(value>>8)&255;
	data[2]=(value>>16)&255;
	data[3]=(value>>24)&255;
}



int rd_word_hl(unsigned char *data)
{
	return (data[0]<<8)+data[1];
}



int rd_dword_hl(unsigned char *data)
{
	return (data[0]<<24)+(data[1]<<16)+(data[2]<<8)+data[3];
}



int rd_var_length(unsigned char *data,int &ptr)
{
	int n,value;

	value=data[ptr++];

	if(value&0x80)
	{
		value&=0x7f;

		while(1)
		{
			n=data[ptr++];

			value=(value<<7)+(n&0x7f);

			if(!(n&0x80)) break;
		}
	}

	return value;
}

