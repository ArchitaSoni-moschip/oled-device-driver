#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DISPLAY_STRING _IOW('a', 'a', char*)
#define ZOOM_IN _IOW('a', 'b', int*)
#define BLINKING _IOW('a', 'c', int*)
#define SCROLLING _IOW('a', 'd', int*)

#define MAX_LIMIT 100
enum {
	EXIT,
	STRING_DISPLAY,
	ZOOM,
	BLINK,
	SCROLL,
	MENU
};

void print_menu (void)
{
        printf ("*****************************************************************************************************************************\n");
        printf ("*****************************************************************************************************************************\n");
	printf ("The OLED module displays string data that is provided by the user and\
 supports graphic modes like scrolling, blinking and\nzooming in.\n");
        printf ("*****************************************************************************************************************************\n");
        printf ("User can configure the OLED module by:\n0. Exit.\n1. Display string.\n2.\
 Enable/Disable zoom in.\n3. Enable/Disable text blinking.\n4. Enable scrolling.\n5. Print menu.\n");
        printf ("*****************************************************************************************************************************\n");
        printf ("*****************************************************************************************************************************\n");
}

int main()
{
	int fd;
	char str [MAX_LIMIT] = {'\0'};
	int on_off_value = 0;
	//printf("Opening Driver...\n");

	fd = open("/dev/oled_device", O_RDWR);
	if(fd < 0) {
		printf("Cannot open device file...\n");
		return 0;
	}

	int choice = 0;

	print_menu ();
	do {
		printf ("Enter your choice:\n");
		scanf ("%d", &choice);
                getchar ();
		switch (choice) {
			case EXIT:
				printf("Closing Driver\n");
				close(fd);
				return 0;
				break;
			case STRING_DISPLAY:
				printf("Enter the string to display:\n");
				fgets (str, MAX_LIMIT, stdin);
				ioctl(fd, DISPLAY_STRING, str);
				break;
			case ZOOM:
				printf("Enter the 1/0 to enable/disable zoom in:\n");
				scanf ("%d", &on_off_value);
				ioctl(fd, ZOOM_IN, (int*)&on_off_value);
				break;
			case BLINK:
				printf("Enter the 1/0 to enable/disable blinking:\n");
				scanf ("%d", &on_off_value);
				ioctl(fd, BLINKING, (int*)&on_off_value);
				break;
			case SCROLL:
				printf("Enter the 1/0 to enable/disable scrolling:\n");
				scanf ("%d", &on_off_value);
				ioctl(fd, SCROLLING, (int*)&on_off_value);
				break;
			case MENU:
				print_menu ();
				break;
			default:
				printf ("Enter a valid choice!!!\n");
				break;
		}
	} while (choice);
	return 0;
}
