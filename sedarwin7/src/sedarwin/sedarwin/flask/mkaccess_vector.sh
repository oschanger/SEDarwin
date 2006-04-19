#!/bin/sh -
#

# FLASK

set -e

#subproject id
subproject="FLASK"

awk=$1
shift

# output files
av_permissions="av_permissions.h"
av_inherit="av_inherit.h"
common_perm_to_string="common_perm_to_string.h"
av_perm_to_string="av_perm_to_string.h"

cat $* | $awk "
BEGIN	{
		outfile = \"$av_permissions\"
		subproject = \"$subproject\"
		inheritfile = \"$av_inherit\"
		cpermfile = \"$common_perm_to_string\"
		avpermfile = \"$av_perm_to_string\"
		"'
		nextstate = "COMMON_OR_AV";
		printf("/* This file is automatically generated.  Do not edit. */\n") > outfile;
		printf("/* This file is automatically generated.  Do not edit. */\n") > inheritfile;
		printf("/* This file is automatically generated.  Do not edit. */\n") > cpermfile;
		printf("/* This file is automatically generated.  Do not edit. */\n") > avpermfile;
;
		printf("/* %s */\n\n", subproject) > outfile;

		printf("/* %s */\n\n", subproject) > cpermfile;

		printf("/* %s */\n\n", subproject) > inheritfile;
		printf("typedef struct\n") > inheritfile;
		printf("{\n") > inheritfile;
		printf("    u16 tclass;\n") > inheritfile;
		printf("    char **common_pts;\n") > inheritfile; 
		printf("    u32 common_base;\n") > inheritfile; 
		printf("} av_inherit_t;\n\n") > inheritfile;
		printf("static av_inherit_t av_inherit[] = {\n") > inheritfile;
	
		printf("/* %s */\n\n", subproject) > avpermfile;
		printf("typedef struct\n") > avpermfile;
		printf("{\n") > avpermfile;
		printf("    u16 tclass;\n") > avpermfile;
		printf("    u32 value;\n") > avpermfile; 
		printf("    char *name;\n") > avpermfile; 
		printf("} av_perm_to_string_t;\n\n") > avpermfile;
		printf("static av_perm_to_string_t av_perm_to_string[] = {\n") > avpermfile;
	}
/^[ \t]*#/	{ 
			next;
		}
$1 == "common"	{ 
			if (nextstate != "COMMON_OR_AV")
			{
				printf("Parse error:  Unexpected COMMON definition on line %d\n", NR);
				next;	
			}

			if ($2 in common_defined)
			{
				printf("Duplicate COMMON definition for %s on line %d.\n", $2, NR);
				next;
			}	
			common_defined[$2] = 1;

			tclass = $2;
			common_name = $2; 
			permission = 1;

			printf("static char *common_%s_perm_to_string[] =\n{\n", $2) > cpermfile;

			nextstate = "COMMON-OPENBRACKET";
			next;
		}
$1 == "class"	{
			if (nextstate != "COMMON_OR_AV" &&
			    nextstate != "CLASS_OR_CLASS-OPENBRACKET")
			{
				printf("Parse error:  Unexpected class definition on line %d\n", NR);
				next;	
			}

			tclass = $2;

			if (tclass in av_defined)
			{
				printf("Duplicate access vector definition for %s on line %d\n", tclass, NR);
				next;
			} 
			av_defined[tclass] = 1;

			inherits = "";
			permission = 1;

			nextstate = "INHERITS_OR_CLASS-OPENBRACKET";
			next;
		}
$1 == "inherits" {			
			if (nextstate != "INHERITS_OR_CLASS-OPENBRACKET")
			{
				printf("Parse error:  Unexpected INHERITS definition on line %d\n", NR);
				next;	
			}

			if (!($2 in common_defined))
			{
				printf("COMMON %s is not defined (line %d).\n", $2, NR);
				next;
			}

			inherits = $2;
			permission = common_base[$2];

			for (combined in common_perms)
			{
				split(combined,separate, SUBSEP);
				if (separate[1] == inherits)
				{
					printf("#define %s__%s", toupper(tclass), toupper(separate[2])) > outfile; 
					spaces = 40 - (length(separate[2]) + length(tclass));
					if (spaces < 1)
					      spaces = 1;
					for (i = 0; i < spaces; i++) 
						printf(" ") > outfile; 
					pt = common_perms[combined];
					printf("0x%08x%08xUL\n", pt>32 ? 2^(pt-33) : 0, pt<33 ? 2^(pt-1) : 0) > outfile;
					#printf("0x%08xUL\n", common_perms[combined]) > outfile; 
				}
			}
			printf("\n") > outfile;
	
			printf("   { SECCLASS_%s, common_%s_perm_to_string, 0x%08x%08xUL },\n", toupper(tclass), inherits,
				permission>32 ? 2^(permission-33) : 0, permission<33 ? 2^(permission-1) : 0) > inheritfile; 

			nextstate = "CLASS_OR_CLASS-OPENBRACKET";
			next;
		}
$1 == "{"	{ 
			if (nextstate != "INHERITS_OR_CLASS-OPENBRACKET" &&
			    nextstate != "CLASS_OR_CLASS-OPENBRACKET" &&
			    nextstate != "COMMON-OPENBRACKET")
			{
				printf("Parse error:  Unexpected { on line %d\n", NR);
				next;
			}

			if (nextstate == "INHERITS_OR_CLASS-OPENBRACKET")
				nextstate = "CLASS-CLOSEBRACKET";

			if (nextstate == "CLASS_OR_CLASS-OPENBRACKET")
				nextstate = "CLASS-CLOSEBRACKET";

			if (nextstate == "COMMON-OPENBRACKET")
				nextstate = "COMMON-CLOSEBRACKET";
		}
/[a-z][a-z_]*/	{
			if (nextstate != "COMMON-CLOSEBRACKET" &&
			    nextstate != "CLASS-CLOSEBRACKET")
			{
				printf("Parse error:  Unexpected symbol %s on line %d\n", $1, NR);		
				next;
			}

			if (nextstate == "COMMON-CLOSEBRACKET")
			{
				if ((common_name,$1) in common_perms)
				{
					printf("Duplicate permission %s for common %s on line %d.\n", $1, common_name, NR);
					next;
				}

				common_perms[common_name,$1] = permission;

				printf("#define COMMON_%s__%s", toupper(common_name), toupper($1)) > outfile; 

				printf("    \"%s\",\n", $1) > cpermfile;
			}
			else
			{
				if ((tclass,$1) in av_perms)
				{
					printf("Duplicate permission %s for %s on line %d.\n", $1, tclass, NR);
					next;
				}

				av_perms[tclass,$1] = permission;
		
				if (inherits != "")
				{
					if ((inherits,$1) in common_perms)
					{
						printf("Permission %s in %s on line %d conflicts with common permission.\n", $1, tclass, inherits, NR);
						next;
					}
				}

				printf("#define %s__%s", toupper(tclass), toupper($1)) > outfile; 

				printf("   { SECCLASS_%s, %s__%s, \"%s\" },\n", toupper(tclass), toupper(tclass), toupper($1), $1) > avpermfile; 
			}

			spaces = 40 - (length($1) + length(tclass));
			if (spaces < 1)
			      spaces = 1;

			for (i = 0; i < spaces; i++) 
				printf(" ") > outfile; 
			printf("0x%08x%08xUL\n", permission>32 ? 2^(permission-33) : 0, permission<33 ? 2^(permission-1) : 0) > outfile; 
			permission = permission + 1;
		}
$1 == "}"	{
			if (nextstate != "CLASS-CLOSEBRACKET" && 
			    nextstate != "COMMON-CLOSEBRACKET")
			{
				printf("Parse error:  Unexpected } on line %d\n", NR);
				next;
			}

			if (nextstate == "COMMON-CLOSEBRACKET")
			{
				common_base[common_name] = permission;
				printf("};\n\n") > cpermfile; 
			}

			printf("\n") > outfile;

			nextstate = "COMMON_OR_AV";
		}
END	{
		if (nextstate != "COMMON_OR_AV" && nextstate != "CLASS_OR_CLASS-OPENBRACKET")
			printf("Parse error:  Unexpected end of file\n");

		printf("\n/* %s */\n", subproject) > outfile;

		printf("\n/* %s */\n", subproject) > cpermfile;
	
		printf("};\n\n") > inheritfile;
		printf("#define AV_INHERIT_SIZE (sizeof(av_inherit)/sizeof(av_inherit_t))\n\n") > inheritfile;
		printf("\n/* %s */\n", subproject) > inheritfile;

		printf("};\n\n") > avpermfile;
		printf("#define AV_PERM_TO_STRING_SIZE (sizeof(av_perm_to_string)/sizeof(av_perm_to_string_t))\n\n") > avpermfile;
		printf("\n/* %s */\n", subproject) > avpermfile;
	}'

# FLASK
