using System.Text.Json;
using System.Windows.Forms;

namespace Allwhither.Windows;

internal static class MenuRenderer
{
    internal static MenuStrip Create(JsonElement menus, ToolStripStatusLabel status)
    {
        var menuStrip = new MenuStrip();
        foreach (JsonElement menu in menus.EnumerateArray())
        {
            var topLevel = new ToolStripMenuItem(UiDefinition.GetString(menu, "label"));
            foreach (JsonElement item in UiDefinition.GetArray(menu, "items").EnumerateArray())
            {
                string text = UiDefinition.GetString(item, "text");
                string message = UiDefinition.GetString(item, "message");
                var menuItem = new ToolStripMenuItem(text);
                menuItem.Click += (_, _) => status.Text = message;
                topLevel.DropDownItems.Add(menuItem);
            }
            menuStrip.Items.Add(topLevel);
        }
        return menuStrip;
    }
}
