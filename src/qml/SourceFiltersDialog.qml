import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sourceFiltersDialog"
    required property var sourceListModel
    required property string sourceId
    readonly property var themeColors: uiTheme.colors
    property var filters: []
    property string selectedId: ""
    readonly property int selectedIndex: filters.findIndex(f => f.id === selectedId)
    readonly property var selectedFilter: selectedIndex >= 0 ? filters[selectedIndex] : null
    implicitWidth: 680
    implicitHeight: 380
    color: themeColors.background.panel
    signal closeRequested()

    function refresh() {
        filters = sourceListModel.sourceFilters(sourceId)
        if (!filters.some(f => f.id === selectedId))
            selectedId = filters.length ? filters[0].id : ""
    }
    function updateParameter(key, value) {
        if (!selectedFilter) return
        sourceListModel.setFilterParameters(sourceId, selectedId,
            key === "brightness" ? value : selectedFilter.brightness,
            key === "contrast" ? value : selectedFilter.contrast,
            key === "saturation" ? value : selectedFilter.saturation)
    }
    Component.onCompleted: refresh()
    Connections {
        target: root.sourceListModel
        function onSourceFiltersChanged(id) { if (id === root.sourceId) root.refresh() }
    }

    component FilterButton: Button {
        id: button
        palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
        icon.width: 16
        icon.height: 16
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.text: text
        background: Rectangle {
            radius: 2
            color: !button.enabled ? root.themeColors.button.background.disabled
                : button.down ? root.themeColors.button.background.pressed
                : button.hovered ? root.themeColors.button.background.hovered
                : root.themeColors.button.background.normal
            border.color: button.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            Label { text: qsTr("滤镜 · 从上到下处理"); color: root.themeColors.text.primary }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.themeColors.background.input
                border.color: root.themeColors.border.default
                ListView {
                    id: filterList
                    objectName: "filterList"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.filters
                    ScrollBar.vertical: ScrollBar {
                        palette.mid: root.themeColors.scrollbar.thumb.normal
                        palette.dark: root.themeColors.scrollbar.thumb.pressed
                    }
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "filterRow_" + modelData.id
                        width: ListView.view.width
                        height: 38
                        text: qsTr("色彩校正 %1").arg(index + 1)
                        palette.text: root.themeColors.text.primary
                        leftPadding: 38
                        background: Rectangle {
                            color: row.modelData.id === root.selectedId ? root.themeColors.background.selected
                                : row.hovered ? root.themeColors.background.hovered : "transparent"
                            border.color: row.activeFocus ? root.themeColors.border.focus : "transparent"
                        }
                        onClicked: root.selectedId = modelData.id
                        FilterButton {
                            objectName: "toggleFilter_" + row.modelData.id
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 30
                            text: row.modelData.enabled ? qsTr("停用滤镜") : qsTr("启用滤镜")
                            display: AbstractButton.IconOnly
                            icon.source: "qrc:/assets/icons/" + (row.modelData.enabled ? "eye" : "eye-off") + ".svg"
                            onClicked: root.sourceListModel.setFilterEnabled(root.sourceId, row.modelData.id, !row.modelData.enabled)
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.filters.length === 0
                        text: qsTr("暂无滤镜")
                        color: root.themeColors.text.secondary
                    }
                }
            }
            RowLayout {
                FilterButton {
                    objectName: "addFilterButton"
                    text: qsTr("添加色彩校正")
                    icon.source: "qrc:/assets/icons/plus.svg"
                    onClicked: root.selectedId = root.sourceListModel.addColorCorrection(root.sourceId)
                }
                Repeater {
                    model: [ { action: "remove", icon: "minus", label: qsTr("删除滤镜") },
                             { action: "up", icon: "arrow-up", label: qsTr("滤镜上移") },
                             { action: "down", icon: "arrow-down", label: qsTr("滤镜下移") } ]
                    delegate: FilterButton {
                        required property var modelData
                        objectName: modelData.action + "FilterButton"
                        text: modelData.label
                        display: AbstractButton.IconOnly
                        icon.source: "qrc:/assets/icons/" + modelData.icon + ".svg"
                        enabled: root.selectedIndex >= 0 && (modelData.action === "remove"
                            || (modelData.action === "up" ? root.selectedIndex > 0 : root.selectedIndex < root.filters.length - 1))
                        onClicked: {
                            if (modelData.action === "remove")
                                root.sourceListModel.removeFilter(root.sourceId, root.selectedId)
                            else root.sourceListModel.moveFilter(root.sourceId, root.selectedId, modelData.action === "up" ? -1 : 1)
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { text: qsTr("色彩校正"); color: root.themeColors.text.primary }
            Label {
                Layout.fillWidth: true
                text: qsTr("修改立即生效，关闭后保留。")
                color: root.themeColors.text.secondary
            }
            Repeater {
                model: [ { key: "brightness", label: qsTr("亮度"), min: -1, max: 1, initial: 0 },
                         { key: "contrast", label: qsTr("对比度"), min: 0, max: 2, initial: 1 },
                         { key: "saturation", label: qsTr("饱和度"), min: 0, max: 2, initial: 1 } ]
                delegate: ColumnLayout {
                    id: parameter
                    required property var modelData
                    Layout.fillWidth: true
                    enabled: root.selectedFilter !== null
                    Label { text: parameter.modelData.label; color: root.themeColors.text.primary }
                    RowLayout {
                        Slider {
                            id: slider
                            objectName: parameter.modelData.key + "Slider"
                            Layout.fillWidth: true
                            from: parameter.modelData.min
                            to: parameter.modelData.max
                            stepSize: 0.01
                            value: root.selectedFilter ? root.selectedFilter[parameter.modelData.key] : parameter.modelData.initial
                            onMoved: root.updateParameter(parameter.modelData.key, value)
                            Accessible.name: parameter.modelData.label
                            background: Rectangle {
                                x: slider.leftPadding
                                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                                width: slider.availableWidth
                                height: 4
                                radius: 2
                                color: root.themeColors.slider.track
                            }
                            handle: Rectangle {
                                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                                width: 14
                                height: 14
                                radius: 7
                                color: slider.enabled ? root.themeColors.slider.handle : root.themeColors.slider.handleDisabled
                                border.color: slider.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
                            }
                        }
                        SpinBox {
                            id: number
                            objectName: parameter.modelData.key + "Input"
                            Layout.preferredWidth: 140
                            from: parameter.modelData.min * 100
                            to: parameter.modelData.max * 100
                            value: Math.round((root.selectedFilter ? root.selectedFilter[parameter.modelData.key] : parameter.modelData.initial) * 100)
                            editable: true
                            Accessible.name: parameter.modelData.label
                            palette.text: root.themeColors.text.primary
                            palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
                            palette.base: root.themeColors.background.input
                            palette.button: root.themeColors.button.background.normal
                            palette.mid: root.themeColors.border.default
                            palette.highlight: root.themeColors.background.selected
                            palette.highlightedText: root.themeColors.text.primary
                            textFromValue: function(value, locale) { return Number(value / 100).toLocaleString(locale, 'f', 2) }
                            valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                            validator: DoubleValidator { bottom: number.from / 100; top: number.to / 100; decimals: 2; locale: number.locale.name }
                            onValueModified: root.updateParameter(parameter.modelData.key, value / 100)
                            background: Rectangle {
                                color: root.themeColors.background.input
                                border.color: number.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
                            }
                        }
                    }
                }
            }
            FilterButton {
                objectName: "resetFilterButton"
                text: qsTr("恢复默认")
                enabled: root.selectedFilter !== null
                onClicked: root.sourceListModel.setFilterParameters(root.sourceId, root.selectedId, 0, 1, 1)
            }
            Item { Layout.fillHeight: true }
            FilterButton {
                objectName: "closeFiltersButton"
                Layout.alignment: Qt.AlignRight
                text: qsTr("关闭")
                onClicked: root.closeRequested()
            }
        }
    }
}
