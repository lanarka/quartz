from prettytable import PrettyTable

CHAR = (8,16) # w,h

SCREENS = (
	("640x480", 640, 480),
	("720x480", 720, 480),
	("720x576", 720, 576),
	("1024x768", 1024, 768),
	("1280x720", 1280, 720),
	("1920x1200", 1920, 1200),
	("1200x1920", 1200, 1920),
)

calc = lambda w,h: "%dx%d" % (w/CHAR[0],h/CHAR[1])

table = PrettyTable(align="r")
table.field_names = [
	"Screen Size",
	"Text Grid",
	"Zoom 1",
	"Zoom 2",
	"Zoom 3",
	"Zoom 4"
]

for _,w,h in SCREENS:
	assert (w%CHAR[0]==0)
	assert (h%CHAR[1]==0)
	done=[_, calc(w,h)]
	for zoom in range(1,5):
		done.append("%dx%d"%(zoom*w,zoom*h))
	table.add_row(done)

print(table)
