
// LBA2CHS.c

void main (int argc, char** argv)
{
  unsigned lba;
  unsigned temp1, temp2;
  unsigned heads = 15;
  unsigned sectorspertrack = 63;

  lba = atol(argv[1]);

  temp1 = heads * sectorspertrack;
  temp2 = lba % temp1;

  printf
  (
    "Cylinder : %d, Head : %d, Sector : %d\n",
    lba / temp1,
    temp2 / sectorspertrack,
    temp2 % sectorspertrack + 1
  );
}
