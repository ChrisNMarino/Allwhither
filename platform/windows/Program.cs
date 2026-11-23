using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows.Forms;

namespace Omni.Windows;

internal static class Program
{
    private const string ProjectName = EmbeddedResourceData.ProjectName;

    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();

        string configPath = $"{ProjectName}.omni";
        try
        {
            JsonObject config = ConfigurationLoader.Load(configPath);
            using JsonDocument document = JsonDocument.Parse(config.ToJsonString());
            JsonElement ui = UiDefinition.GetObject(document.RootElement, "ui");
            JsonElement window = UiDefinition.GetObject(ui, "window");
            JsonElement layout = UiDefinition.GetObject(window, "layout");
            string layoutType = UiDefinition.GetString(layout, "type");
            if (layoutType is not ("vertical" or "center"))
            {
                throw new InvalidDataException("window.layout.type must be 'vertical' or 'center'.");
            }

            string title = UiDefinition.GetString(window, "title");
            int width = UiDefinition.GetPositiveInt(window, "width");
            int height = UiDefinition.GetPositiveInt(window, "height");
            bool fullscreen = UiDefinition.OptionalBoolean(window, "fullscreen");
            bool fullscreenCenter = fullscreen && layoutType == "center";
            JsonElement menus = UiDefinition.GetArray(ui, "menus");
            JsonElement controls = UiDefinition.GetObject(ui, "controls");
            JsonElement controlOrder = UiDefinition.GetArray(layout, "children");
            UiDefinition.ValidateMenus(menus);
            UiDefinition.ValidateControlTree(controls, controlOrder);
            UiDefinition.ThemeColors theme = UiDefinition.GetTheme(ui);
            UiDefinition.ThemeColors chrome = UiDefinition.GetChromeColors(window);

            var form = new Form
            {
                Text = title,
                ClientSize = new Size(width, height),
                StartPosition = FormStartPosition.CenterScreen,
                FormBorderStyle = fullscreen ? FormBorderStyle.None : FormBorderStyle.Sizable,
                WindowState = fullscreen ? FormWindowState.Maximized : FormWindowState.Normal
            };
            var status = new ToolStripStatusLabel("Ready");
            var statusStrip = new StatusStrip();
            statusStrip.Items.Add(status);
            var dom = new UiDom(controls, status);
            JsonElement? scripts = document.RootElement.TryGetProperty("scripts", out JsonElement scriptsElement)
                ? scriptsElement
                : null;
            var actionRuntime = new JavaScriptActionRuntime(controls, dom, scripts);
            actionRuntime.Prepare();

            var controlRenderer = new ControlRenderer(
                controls, status, dom, actionRuntime, width - 48, theme);
            if (layoutType == "center")
            {
                var centeredLayout = new TableLayoutPanel
                {
                    Dock = DockStyle.Fill,
                    ColumnCount = 1,
                    RowCount = 1
                };
                centeredLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
                centeredLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
                var centeredControls = new FlowLayoutPanel
                {
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Anchor = AnchorStyles.None
                };
                foreach (JsonElement controlId in controlOrder.EnumerateArray())
                {
                    centeredControls.Controls.Add(controlRenderer.Render(controlId.GetString()!));
                }
                dom.AttachMount(centeredControls, controlRenderer.Render);
                centeredLayout.Controls.Add(centeredControls, 0, 0);
                if (fullscreenCenter)
                {
                    form.Controls.Add(centeredLayout);
                }
                else
                {
                    var applicationLayout = new TableLayoutPanel
                    {
                        Dock = DockStyle.Fill,
                        ColumnCount = 1,
                        RowCount = 3
                    };
                    applicationLayout.ColumnStyles.Add(
                        new ColumnStyle(SizeType.Percent, 100));
                    applicationLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                    applicationLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
                    applicationLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                    var header = new FlowLayoutPanel
                    {
                        AutoSize = true,
                        Dock = DockStyle.Fill,
                        FlowDirection = FlowDirection.TopDown,
                        WrapContents = false
                    };
                    form.MainMenuStrip = MenuRenderer.Create(menus, status);
                    header.Controls.Add(form.MainMenuStrip);
                    header.Controls.Add(new Label
                    {
                        Text = title,
                        Font = new Font(SystemFonts.DefaultFont.FontFamily, 14),
                        AutoSize = true
                    });
                    applicationLayout.Controls.Add(header, 0, 0);
                    applicationLayout.Controls.Add(centeredLayout, 0, 1);
                    applicationLayout.Controls.Add(statusStrip, 0, 2);
                    form.Controls.Add(applicationLayout);
                }
            }
            else
            {
                var controlList = new FlowLayoutPanel
                {
                    Dock = DockStyle.Fill,
                    AutoScroll = true,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Padding = new Padding(12)
                };
                controlList.Controls.Add(new Label
                {
                    Text = title,
                    Font = new Font(SystemFonts.DefaultFont.FontFamily, 14),
                    AutoSize = true,
                    Margin = new Padding(3, 3, 3, 10)
                });
                foreach (JsonElement controlId in controlOrder.EnumerateArray())
                {
                    controlList.Controls.Add(controlRenderer.Render(controlId.GetString()!));
                }
                dom.AttachMount(controlList, controlRenderer.Render);
                form.Controls.Add(controlList);
                form.Controls.Add(statusStrip);
                form.MainMenuStrip = MenuRenderer.Create(menus, status);
                form.Controls.Add(form.MainMenuStrip);
            }
            PaintChrome(form, statusStrip, status, chrome);
            Application.Run(form);
        }
        catch (Exception exception) when (
            exception is IOException or JsonException or InvalidDataException)
        {
            MessageBox.Show(exception.Message, "omni configuration error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static void PaintChrome(
        Form form, StatusStrip statusStrip, ToolStripStatusLabel status,
        UiDefinition.ThemeColors chrome)
    {
        if (chrome.Background is Color background)
        {
            form.BackColor = background;
            statusStrip.RenderMode = ToolStripRenderMode.Professional;
            statusStrip.BackColor = background;
            if (form.MainMenuStrip != null)
            {
                form.MainMenuStrip.RenderMode = ToolStripRenderMode.Professional;
                form.MainMenuStrip.BackColor = background;
            }
        }
        if (chrome.Foreground is Color foreground)
        {
            form.ForeColor = foreground;
            status.ForeColor = foreground;
            statusStrip.ForeColor = foreground;
            if (form.MainMenuStrip != null)
            {
                form.MainMenuStrip.ForeColor = foreground;
                foreach (ToolStripItem item in form.MainMenuStrip.Items)
                {
                    item.ForeColor = foreground;
                }
            }
        }
    }
}
