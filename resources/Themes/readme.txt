        Theming Support
        ---------------

There are 2 theme folder, one a read only for themes supplied by the installer.
And a second read-write location for user supplied themes. To discover the location
of these folder using the "..." button in File -> Options -> Appearence -> Theme.

Each of these folders can have sub-folders that contain one or more theme files.
The name of the folder is used to name the theme.

For icon filenames you can put the icon size after the dash. e.g.

	Toolbar-24.png - would be 24px wide by 24px high icons.
	Toolbar-32x24.png - would be 32px wide by 24px high icons.
	
	All icons need to be on the same row and in the same order as the system icon
	file.

The types of files you can put in a theme folder are:

    Icons-###.png - alternative icons for the list view.

    Toolbar-###.png - alternative icons for the toolbars.

    colours.json - alternative system colours. See the 'Dark'
                theme for an example of how to format. The colours
                themselves are standard CSS colours.
    
    styles.css - additional CSS styles for controls. This allows you
                to add more than just colours to controls. E.g. image
                backgrounds. However the CSS support acress the app is
                spotty to say the least. Some control names to get
                started with:
                    
                    LList, GTree, GToolBar, GToolButton, 
                    GPanel, ScribeWnd, MailUi, MailUiGpg,
                    GButton, GCheckBox, GRadioButton, GRadioGroup

                Mind you on windows some of the system controls can't
                be themed. Because the OS draws them.