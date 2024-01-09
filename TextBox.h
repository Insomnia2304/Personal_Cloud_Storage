#include <SFML/Graphics.hpp>
#include <iostream>

class TextBox {
public:
    TextBox(sf::Font& font, unsigned int characterSize, sf::Vector2f position, unsigned int maxWidth, unsigned int maxHeight)
        : font(font), characterSize(characterSize), position(position), maxWidth(maxWidth), maxHeight(maxHeight) {
        text.setString("");
        text.setFont(font);
        text.setCharacterSize(characterSize);
        text.setPosition(position);
        text.setFillColor(sf::Color::Black);
    }

    void setText(const std::string& str) {
        originalText = str;
    }

    std::string getText() {
        return originalText;
    }

    void setPosition(sf::Vector2f newPosition) {
        position = newPosition;
        text.setPosition(position);
    }

    void setMaxWidth(unsigned int newMaxWidth) {
        maxWidth = newMaxWidth;
    }

    void draw(sf::RenderWindow& window, bool isPassword = false) {
        updateText();
        if (isPassword == true) {
            sf::String oldString(text.getString());
            sf::String newString(oldString);
            
            int start = oldString.find(" ");
            // size - 1 because of the '_' which is appended at the end
            for (int i = start + 1; i < oldString.getSize() - 1; i++)
                newString[i] = '*';
            text.setString(newString);
        }
        window.draw(text);
    }

private:
    void updateText() {
        std::string wrappedText;
        unsigned int lineWidth = 0;
        float totalHeight = characterSize;

        for (char c : originalText) {
            sf::Glyph glyph = font.getGlyph(c, characterSize, false);

            if (lineWidth + glyph.advance > maxWidth || c == '\n') {
                if (c != '\n') wrappedText += "-\n";
                lineWidth = 0;
                totalHeight += font.getLineSpacing(characterSize);
                if (totalHeight > maxHeight)
                    break;
            }
            wrappedText += c;
            lineWidth += glyph.advance;
        }
        text.setString(wrappedText);
    }

    sf::Font& font;
    unsigned int characterSize;
    sf::Vector2f position;
    unsigned int maxWidth;
    unsigned int maxHeight;
    std::string originalText;
    sf::Text text;
};