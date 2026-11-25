using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows.Forms;

namespace Allwhither.Windows;

internal static class Program
{
    private const string ProjectName = EmbeddedResourceData.ProjectName;

    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();

        string configPath = $"{ProjectName}.json";
        try
        {
            JsonObject config = ConfigurationLoader.Load(configPath);
            using JsonDocument document = JsonDocument.Parse(config.ToJsonString());
            JsonElement ui = UiDefinition.GetObject(document.RootElement, "ui");
            JsonElement window = UiDefinition.GetObject(ui, "window");
            JsonElement layout = UiDefinition.GetObject(window, "layout");
            string layoutType = UiDefinition.GetString(layout, "type");
            UiDefinition.ValidateLayout(layout, true, "window.layout");

            string title = UiDefinition.GetString(window, "title");
            int width = UiDefinition.GetPositiveInt(window, "width");
            int height = UiDefinition.GetPositiveInt(window, "height");
            bool fullscreen = UiDefinition.OptionalBoolean(window, "fullscreen");
            bool fullscreenCenter = fullscreen && layoutType == "center";
            JsonElement menus = UiDefinition.GetArray(ui, "menus");
            bool hasMenus = menus.GetArrayLength() > 0;
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
                controls, status, dom, actionRuntime, width, theme);
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
                void RelayoutCentered()
                {
                    controlRenderer.ApplyWidth(
                        centeredControls,
                        Math.Max(1, centeredLayout.ClientSize.Width),
                        form.ClientSize.Width,
                        centeredLayout.ClientSize.Height);
                }
                centeredLayout.SizeChanged += (_, _) => RelayoutCentered();
                RelayoutCentered();
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
                        RowCount = hasMenus ? 3 : 2
                    };
                    applicationLayout.ColumnStyles.Add(
                        new ColumnStyle(SizeType.Percent, 100));
                    if (hasMenus)
                    {
                        applicationLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                    }
                    applicationLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
                    applicationLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                    if (hasMenus)
                    {
                        var header = new FlowLayoutPanel
                        {
                            AutoSize = true,
                            Dock = DockStyle.Fill,
                            FlowDirection = FlowDirection.TopDown,
                            WrapContents = false
                        };
                        form.MainMenuStrip = MenuRenderer.Create(menus, status);
                        header.Controls.Add(form.MainMenuStrip);
                        applicationLayout.Controls.Add(header, 0, 0);
                    }
                    int controlsRow = hasMenus ? 1 : 0;
                    applicationLayout.Controls.Add(centeredLayout, 0, controlsRow);
                    applicationLayout.Controls.Add(statusStrip, 0, controlsRow + 1);
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
                    WrapContents = false
                };
                var body = new FlowLayoutPanel
                {
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Margin = new Padding(0)
                };
                foreach (JsonElement controlId in controlOrder.EnumerateArray())
                {
                    body.Controls.Add(controlRenderer.Render(controlId.GetString()!));
                }
                controlRenderer.RegisterGroup(body, layout);
                controlList.Controls.Add(body);
                dom.AttachMount(controlList, controlRenderer.Render);
                form.Controls.Add(controlList);
                form.Controls.Add(statusStrip);
                if (hasMenus)
                {
                    form.MainMenuStrip = MenuRenderer.Create(menus, status);
                    form.Controls.Add(form.MainMenuStrip);
                }
                void Relayout()
                {
                    controlList.SuspendLayout();
                    controlRenderer.ApplyWidth(
                        controlList,
                        Math.Max(1, controlList.ClientSize.Width),
                        form.ClientSize.Width,
                        Math.Max(1, controlList.ClientSize.Height));
                    controlList.ResumeLayout(true);
                }
                form.ClientSizeChanged += (_, _) => Relayout();
                Relayout();
            }
            PaintChrome(form, statusStrip, status, chrome);
            Application.Run(form);
        }
        catch (Exception exception) when (
            exception is IOException or JsonException or InvalidDataException)
        {
            MessageBox.Show(exception.Message, "allwhither configuration error",
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
